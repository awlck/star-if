// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/instance_properties.hpp"

#include <algorithm>
#include <vector>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/property.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/schema/types.hpp"

#include "starcore/placement.hpp"

namespace starcore {

namespace {

using stardata::ast::Block;
using stardata::ast::Statement;
using stardata::ast::Value;
using stardata::diag::Code;
using stardata::diag::Diagnostic;
using stardata::diag::DiagnosticSink;
using stardata::schema::ClassDecl;
using stardata::schema::PropDecl;
using stardata::schema::SchemaSet;

// The keys §7.4 permits that are not properties at all, so no class walk
// will ever find them. `holder`, `relation` and `sector` are deliberately
// NOT here -- F13 made them ordinary properties of `starcore.object`, and
// every class descends from it, so the class walk already accepts them.
//
// `of_class` only appears in the long `object = { ... }` spelling, where it
// is one of that form's own schema keys and already validated as a
// `ref<class>` before this pass ever runs; it is skipped unconditionally
// because a class declaring a property of the same name would be a
// vanishingly strange thing to do and not a case worth two code paths for.
constexpr std::string_view kId = "id";
constexpr std::string_view kTraits = "traits";
constexpr std::string_view kPropDef = "prop_def";
constexpr std::string_view kOfClass = "of_class";

[[nodiscard]] bool is_structural_key(std::string_view key) {
    return key == kId || key == kTraits || key == kPropDef || key == kOfClass;
}

void check_block(const Block& block, const ClassDecl& type, const SchemaSet& set,
                 DiagnosticSink& sink) {
    // A throwaway sink: whatever these declarations are wrong about,
    // `check_instantiation` (schema/types.cpp) already said so when the
    // schema layer loaded this same block, and saying it twice would double
    // every diagnostic in the corpus. `note_local_properties` in
    // libs/stardata/src/schema/loader.cpp takes the identical precaution for
    // the identical reason.
    stardata::diag::DiagnosticSink quiet;
    const std::vector<PropDecl> local =
        stardata::schema::read_local_prop_defs(block, set.find("prop_marker"), quiet);

    const std::vector<std::string>& relations = relation_keywords(set);
    const auto is_relation_keyword = [&relations](std::string_view key) {
        return std::find(relations.begin(), relations.end(), key) != relations.end();
    };

    for (const Statement& statement : block.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key || key->empty()) {
            continue;
        }
        if (is_structural_key(*key) || is_relation_keyword(*key)) {
            continue;
        }
        if (stardata::schema::resolve_property(*key, local, type, set) != nullptr) {
            continue; // an ordinary property, own or inherited -- F4's to type-check
        }

        Diagnostic diagnostic(Code::PropUnknown, statement.report_span(),
                              "'" + *key + "' is not a property of '" + type.id + "'");
        diagnostic.with_note("an instantiation's keys are the class's properties, plus `id`, "
                             "`traits`, and one of §8.5's placement keywords -- a key naming "
                             "none of those is a typo, not a one-off property (spec §7.4, §8.7)");

        std::vector<std::string_view> candidates = stardata::schema::reachable_properties(type, set);
        for (const PropDecl& own : local) {
            if (std::find(candidates.begin(), candidates.end(), own.name) == candidates.end()) {
                candidates.emplace_back(own.name);
            }
        }
        stardata::schema::suggest(
            diagnostic, statement.key() ? statement.key()->span() : statement.report_span(), *key,
            candidates);
        sink.report(std::move(diagnostic));
    }
}

} // namespace

void check_instance_properties(const stardata::ast::File& file, const SchemaSet& set,
                               DiagnosticSink& sink) {
    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key || key->empty()) {
            continue;
        }
        const ClassDecl* type = stardata::schema::read_object_class(statement, *key, set);
        if (type == nullptr) {
            continue; // a form, a trait mixed in by name, or nothing the set knows
        }
        const std::optional<Value> value = statement.value();
        if (const std::optional<Block> block = value ? value->as_block() : std::nullopt) {
            check_block(*block, *type, set, sink);
        }
    }
}

} // namespace starcore
