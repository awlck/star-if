// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/placement.hpp"

#include "stardata/diag/diagnostic.hpp"

namespace starcore {

namespace {

using stardata::ast::Block;
using stardata::ast::Scalar;
using stardata::ast::Statement;
using stardata::ast::Value;
using stardata::diag::Code;
using stardata::diag::Diagnostic;

// The id an identifier-valued statement names, or nothing.
[[nodiscard]] std::string target_of(const Statement& statement) {
    const std::optional<Value> value = statement.value();
    const std::optional<Scalar> scalar = value ? value->as_scalar() : std::nullopt;
    const std::optional<std::string_view> id = scalar ? scalar->as_identifier() : std::nullopt;
    return id ? std::string(*id) : std::string();
}

} // namespace

const std::vector<std::string>& relation_keywords(const stardata::schema::SchemaSet& set) {
    static const std::vector<std::string> none;
    const stardata::schema::EnumDecl* declared = set.find_enum(relation_enum_id);
    return declared != nullptr ? declared->values : none;
}

std::optional<Placement> read_placement(const Block& block, const stardata::schema::SchemaSet& set,
                                        stardata::diag::DiagnosticSink& sink) {
    const std::vector<std::string>& relations = relation_keywords(set);
    const auto is_relation = [&relations](std::string_view key) {
        for (const std::string& relation : relations) {
            if (relation == key) {
                return true;
            }
        }
        return false;
    };

    // The long form first: `holder` and `relation` are what the two slots
    // actually are (§8.1.1), and the keywords are shorthand for setting them.
    const std::optional<Statement> holder = block.find("holder");
    const std::optional<Statement> relation = block.find("relation");

    std::vector<Statement> sugar;
    for (const Statement& statement : block.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (key && is_relation(*key)) {
            sugar.push_back(statement);
        }
    }

    // §8.5: writing both spellings assigns the same two slots twice, and the
    // conflict is not resolvable by precedence -- so neither wins here
    // either. Guessing would put the object somewhere the author did not ask
    // for, which is worse than refusing.
    if (!sugar.empty() && (holder || relation)) {
        const Statement& other = holder ? *holder : *relation;
        Diagnostic diagnostic(Code::PlacementConflict, sugar.front().report_span(),
                              "this object is placed twice, once with '" +
                                  *sugar.front().key_name() + "' and once with '" +
                                  *other.key_name() + "'");
        diagnostic.with_note("`" + *sugar.front().key_name() +
                                 " = ...` is shorthand for setting `holder` and `relation`, so "
                                 "these are the same two slots -- and there is no sensible "
                                 "precedence between them (spec §8.5)",
                             other.report_span());
        diagnostic.with_fix_it(other.report_span(), "", "keep one spelling and remove the other");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    // Two relation keywords are the same conflict wearing one spelling.
    if (sugar.size() > 1) {
        Diagnostic diagnostic(Code::PlacementConflict, sugar[1].report_span(),
                              "this object is placed twice, with '" + *sugar[0].key_name() +
                                  "' and with '" + *sugar[1].key_name() + "'");
        diagnostic.with_note("an object has at most one parent, and one relation to it "
                             "(spec §8.5)",
                             sugar[0].report_span());
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    if (!sugar.empty()) {
        Placement placement;
        placement.relation = *sugar.front().key_name();
        placement.holder = target_of(sugar.front());
        placement.from_sugar = true;
        placement.span = sugar.front().report_span();
        return placement;
    }

    if (!holder && !relation) {
        return std::nullopt; // a root object, or one placed at run time
    }

    Placement placement;
    placement.span = holder ? holder->report_span() : relation->report_span();
    if (holder) {
        placement.holder = target_of(*holder);
    }
    if (relation) {
        placement.relation = target_of(*relation);
    }
    return placement;
}

void check_placements(const stardata::ast::File& file, const stardata::schema::SchemaSet& set,
                      stardata::diag::DiagnosticSink& sink) {
    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key || key->empty()) {
            continue;
        }
        // §7.4: a top-level statement instantiates a class, in either of the
        // two spellings. Only an object has a placement, so only an
        // instantiation is asked about it -- and asking the format layer
        // rather than testing `find_class` here is what keeps this pass from
        // needing to know there are two spellings at all.
        if (stardata::schema::read_object_class(statement, *key, set) == nullptr) {
            continue;
        }
        const std::optional<Value> value = statement.value();
        if (const std::optional<Block> block = value ? value->as_block() : std::nullopt) {
            (void)read_placement(*block, set, sink);
        }
    }
}

} // namespace starcore
