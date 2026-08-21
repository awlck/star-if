// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/globals.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/schema/types.hpp"

namespace starcore {
namespace {

using stardata::ast::Block;
using stardata::ast::Scalar;
using stardata::ast::Statement;
using stardata::ast::Value;
using stardata::diag::Code;
using stardata::diag::Diagnostic;
using stardata::diag::DiagnosticSink;
using stardata::schema::SchemaSet;

// §7.2.4's save-state forms, and the keys inside them.
constexpr std::string_view kGlobalForm = "global";
constexpr std::string_view kConstForm = "const";
constexpr std::string_view kId = "id";
constexpr std::string_view kType = "type";
constexpr std::string_view kInitial = "initial";
constexpr std::string_view kValue = "value";

// §6.4.1's sugar, and §11.1's two general writes.
constexpr std::string_view kSetFlag = "set_flag";
constexpr std::string_view kClearFlag = "clear_flag";
constexpr std::string_view kFlagSet = "flag_set";
constexpr std::string_view kSetGlobal = "set_global";
constexpr std::string_view kAddGlobal = "add_global";

[[nodiscard]] bool is_flag_write(std::string_view key) noexcept {
    return key == kSetFlag || key == kClearFlag;
}

[[nodiscard]] bool is_global_write(std::string_view key) noexcept {
    return key == kSetGlobal || key == kAddGlobal;
}

[[nodiscard]] std::optional<std::string> identifier_of(const Statement& statement) {
    const std::optional<Value> value = statement.value();
    const std::optional<Scalar> scalar = value ? value->as_scalar() : std::nullopt;
    const std::optional<std::string_view> name = scalar ? scalar->as_identifier() : std::nullopt;
    return name ? std::optional<std::string>(*name) : std::nullopt;
}

} // namespace

const GlobalIndex::Declaration* GlobalIndex::find(std::string_view id) const noexcept {
    for (const Declaration& declared : declarations_) {
        if (declared.id == id) {
            return &declared;
        }
    }
    return nullptr;
}

void GlobalIndex::add_file(const stardata::ast::File& file, const SchemaSet& set,
                           DiagnosticSink& sink) {
    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        const std::optional<Value> value = statement.value();
        const std::optional<Block> block = value ? value->as_block() : std::nullopt;
        if (!key || !block) {
            continue;
        }
        if (*key == kGlobalForm || *key == kConstForm) {
            read_declaration(*block, *key == kConstForm, set, sink);
            continue;
        }
        walk(*block);
    }
}

void GlobalIndex::read_declaration(const Block& block, bool is_const, const SchemaSet& set,
                                   DiagnosticSink& sink) {
    const std::optional<Statement> id = block.find(kId);
    const std::optional<std::string> name = id ? identifier_of(*id) : std::nullopt;
    const std::optional<Value> declared_type = block.value_of(kType);
    if (!name || !declared_type) {
        return; // E-KEY-MISSING or E-TYPE-MISMATCH already said so
    }
    const std::optional<stardata::ast::TypeRef> type = declared_type->as_type();
    if (!type) {
        return;
    }

    // The gap this closes: a global's declared type was validated as being
    // *spelled* like a type expression and never checked to name one, because
    // `check_declared_types` walks schemas and classes and globals are
    // neither. `global = { id = x  type = frobnicate }` loaded clean.
    stardata::schema::check_type(
        *type, declared_type->span(),
        "the " + std::string(is_const ? "const" : "global") + " '" + *name + "'", set, sink);

    // And the initial value against that type, which is the check the `any`
    // on those two keys defers to here (§6.4: "Both are typed, using the same
    // types as properties (§6.2), including collections").
    const std::string_view initial_key = is_const ? kValue : kInitial;
    if (const std::optional<Value> initial = block.value_of(initial_key)) {
        stardata::schema::check_value("the " + std::string(initial_key) + " value of '" + *name +
                                          "'",
                                      *initial, *type, set, sink);
    }

    // The span is the id's VALUE, not the `id` key: the name is what a
    // reader is looking for, and underlining `id` would be the same three
    // characters on every global in the file.
    const std::optional<Value> written = id->value();
    declarations_.push_back(
        Declaration{*name, *type, is_const, written ? written->span() : id->report_span()});
}

void GlobalIndex::note(std::string id, stardata::diag::Span span, Kind kind) {
    uses_.push_back(Use{std::move(id), span, kind});
}

void GlobalIndex::walk(const Block& block) {
    for (const Statement& statement : block.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key) {
            continue;
        }
        const std::optional<Value> value = statement.value();
        const std::optional<Block> inner = value ? value->as_block() : std::nullopt;

        // §6.4.1's sugar. `set_flag` and `clear_flag` write; `flag_set` reads.
        if (is_flag_write(*key) || *key == kFlagSet) {
            if (const std::optional<std::string> named = identifier_of(statement)) {
                note(*named, value->span(), *key == kFlagSet ? Kind::FlagRead : Kind::FlagWrite);
            }
            continue;
        }

        // §11.1's `set_global = { id = … value = … }` and `add_global`. The
        // `id` names the global and is a write; everything else in the block
        // is an ordinary value and is walked.
        if (is_global_write(*key) && inner) {
            if (const std::optional<Statement> target = inner->find(kId)) {
                if (const std::optional<std::string> named = identifier_of(*target)) {
                    const std::optional<Value> at = target->value();
                    note(*named, at ? at->span() : target->report_span(), Kind::Write);
                }
            }
            for (const Statement& nested : inner->statements()) {
                if (nested.key_name() == kId) {
                    continue; // already counted, and as a write rather than a read
                }
                if (const std::optional<Value> held = nested.value()) {
                    if (const std::optional<Block> body = held->as_block()) {
                        walk(*body);
                    }
                }
            }
            continue;
        }

        // §6.4's namespace block: `global = { alert_level == high }`, where
        // each KEY names a global and testing it is a read. Only nested --
        // at the top level `global` is the declaration form, and `add_file`
        // has already taken that branch.
        if (*key == kGlobalForm && inner) {
            for (const Statement& tested : inner->statements()) {
                if (const std::optional<std::string> named = tested.key_name()) {
                    note(*named, tested.key() ? tested.key()->span() : tested.report_span(),
                         Kind::Read);
                }
            }
            continue;
        }

        if (inner) {
            walk(*inner);
            continue;
        }

        // Anything else naming a declared id is counted as a read, and this
        // is deliberately generous. §6.6.3 makes "a bare identifier in an
        // argument position always a global", so `collection = seen_endings`
        // and `value_of = core_temp` (§10.6.1) are reads -- but telling those
        // apart from an unrelated identifier of the same name needs §6.6's
        // datum resolution, which is backlog F9's.
        //
        // Erring toward calling something a read is the right direction for a
        // warning: a missed W-GLOBAL-UNUSED costs nothing, and a false one is
        // noise on correct data. The recognised WRITE sites above are exact,
        // so "set and never tested" is still caught.
        if (const std::optional<std::string> named = identifier_of(statement)) {
            note(*named, value->span(), Kind::Mentioned);
        }
    }

    // A list block's bare entries, for the same reason.
    for (const Scalar& entry : block.values()) {
        if (const std::optional<std::string_view> named = entry.as_identifier()) {
            note(std::string(*named), entry.span(), Kind::Mentioned);
        }
    }
}

void GlobalIndex::check(DiagnosticSink& sink) const {
    std::vector<std::string_view> candidates;
    candidates.reserve(declarations_.size());
    for (const Declaration& declared : declarations_) {
        candidates.emplace_back(declared.id);
    }

    std::set<std::string> read;
    for (const Use& use : uses_) {
        const Declaration* declared = find(use.id);

        if (declared != nullptr && use.reads()) {
            read.insert(use.id);
        }

        // A use that names nothing is reported only where the site names a
        // global for certain. `Mentioned` is the inference, and inferring an
        // error from it would report every identifier in the file that
        // happens not to be a global.
        if (declared == nullptr) {
            if (!use.certain()) {
                continue;
            }
            Diagnostic diagnostic(use.flag_sugar() ? Code::FlagUndeclared : Code::GlobalUndeclared,
                                  use.span, "nothing declares a global called '" + use.id + "'");
            diagnostic.with_note(
                use.flag_sugar()
                    ? "a flag is sugar over a declared `bool` global, not a separate store -- "
                      "which is what makes `set_flag = captain_found` against `flag_set = "
                      "captain_finded` a compile error instead of a condition that never fires "
                      "(spec §6.4.1)"
                    : "a global or const must be declared before it is named; there is no "
                      "implicit creation (spec §6.4)");
            stardata::schema::suggest(diagnostic, use.span, use.id, candidates);
            sink.report(std::move(diagnostic));
            continue;
        }

        if (use.flag_sugar() && !declared->is_bool()) {
            Diagnostic diagnostic(Code::FlagNotBool, use.span,
                                  "'" + use.id + "' is declared " + declared->type.to_string() +
                                      ", and the flag sugar only reads and writes `bool`");
            diagnostic.with_note("the flag forms are exactly `set_global = { id = " + use.id +
                                     "  value = yes }` and `global = { " + use.id +
                                     " == yes }`, so a global of another type has no flag "
                                     "spelling (spec §6.4.1)",
                                 declared->span);
            sink.report(std::move(diagnostic));
        }
    }

    for (const Declaration& declared : declarations_) {
        if (read.contains(declared.id)) {
            continue;
        }
        Diagnostic diagnostic(Code::GlobalUnused, declared.span,
                              "nothing reads the " +
                                  std::string(declared.is_const ? "const" : "global") + " '" +
                                  declared.id + "'");
        diagnostic.with_note(declared.is_const
                                 ? "a const exists so that a tuning value has a name and a place "
                                   "(spec §6.4); one nothing names is a value with neither"
                                 : "a global that is written and never tested is world state "
                                   "nothing depends on -- usually a condition that was renamed, "
                                   "or one that was never written (spec §6.4)");
        sink.report(std::move(diagnostic));
    }
}

} // namespace starcore
