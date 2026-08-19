// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/narrowing.hpp"

#include <algorithm>
#include <map>
#include <optional>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/suggest.hpp"

namespace starcore {

namespace {

using stardata::ast::Block;
using stardata::ast::Statement;
using stardata::diag::Code;
using stardata::diag::Diagnostic;
using stardata::schema::ClassDecl;
using stardata::schema::PropertyAnswer;
using stardata::schema::PropertyLookup;
using stardata::schema::SchemaSet;

// The root of the object model (§8.1.1). Core's own, and so nameable here.
constexpr std::string_view kRootClass = "starcore.object";

// §8.8.1: "`location` | the acting actor's current room -- `room`". The
// spec writes stdlib's name; core's own room class is the honest static
// type, since §7.2.4 says stdlib "could be replaced wholesale".
constexpr std::string_view kRoomClass = "starcore.room";

// The key by which a rule names the action it responds to (§7.2.4's `rule`
// form). A rule has no grammar of its own, so §8.8.1's "determined by the
// action's grammar token" has to reach through this.
constexpr std::string_view kOfAction = "of_action";

// §10.4's three narrowing predicates, plus the explicit escape of §8.8.3.
constexpr std::string_view kOfClass = "of_class";
constexpr std::string_view kHasTrait = "has_trait";
constexpr std::string_view kIs = "is";
constexpr std::string_view kHasProp = "has_prop";

// §10.3's combinators. `OR` and `NOT` are barriers: §8.8.3 says narrowing
// "does not survive an `OR` branch, since only one branch is known to have
// held", and that a narrowing "established inside a `NOT` does not escape
// it". `AND` is the explicit form of the default and is transparent.
constexpr std::string_view kOr = "OR";
constexpr std::string_view kNot = "NOT";
constexpr std::string_view kAnd = "AND";
constexpr std::string_view kCountAtLeast = "COUNT_AT_LEAST";

// A key that is a comparison rather than a binding is a property TEST, which
// is the read §8.8 is about (§10.2: "a key naming a property of that object
// with a comparison operator tests it").
[[nodiscard]] bool is_comparison(std::string_view op) noexcept {
    return op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
}

// What each slot is narrowed to, as the walk proceeds. Keyed by slot name;
// absent means "still whatever the grammar token said".
using Narrowing = std::map<std::string, std::string>;

[[nodiscard]] const ClassDecl* type_of(const std::string& slot, const Narrowing& narrowed,
                                       const std::vector<Slot>& slots, const SchemaSet& set) {
    const auto it = narrowed.find(slot);
    if (it != narrowed.end()) {
        return set.find_class_or_trait(it->second);
    }
    for (const Slot& declared : slots) {
        if (declared.name == slot) {
            return set.find_class_or_trait(declared.type_id);
        }
    }
    return nullptr;
}

[[nodiscard]] std::string list_of(const std::vector<std::string>& names) {
    std::string text;
    const std::size_t shown = std::min<std::size_t>(names.size(), 6);
    for (std::size_t i = 0; i < shown; ++i) {
        text += i > 0 ? ", " : "";
        text += names[i];
    }
    if (shown < names.size()) {
        text += ", ...";
    }
    return text;
}

// The value of a statement as a bare identifier, which is the shape every
// narrowing predicate's argument has.
[[nodiscard]] std::string identifier_value(const Statement& statement) {
    const std::optional<stardata::ast::Value> value = statement.value();
    const std::optional<stardata::ast::Scalar> scalar = value ? value->as_scalar() : std::nullopt;
    const std::optional<std::string_view> text = scalar ? scalar->as_identifier() : std::nullopt;
    return text ? std::string(*text) : std::string();
}

struct Walk {
    const SchemaSet& set;
    const std::vector<Slot>& slots;
    stardata::diag::DiagnosticSink& sink;

    // One object-scoped block (§10.2): `noun = { of_class = weapon  damage > 3 }`.
    //
    // `narrowed` is carried in and out, because a narrowing earlier in a
    // conjunction applies to everything after it -- §10.1's evaluation is
    // ordered and short-circuiting, which is exactly what makes that sound.
    void object_scope(const std::string& slot, const Block& block, Narrowing& narrowed,
                      bool may_narrow) {
        for (const Statement& statement : block.statements()) {
            const std::optional<std::string> key = statement.key_name();
            if (!key || key->empty()) {
                continue;
            }

            // A narrowing predicate. `of_class` and `is` give a class; a
            // `has_trait` gives a trait, which §8.4 resolves properties
            // through just as well.
            if (*key == kOfClass || *key == kHasTrait || *key == kIs) {
                const std::string named = identifier_value(statement);
                if (may_narrow && !named.empty() && set.find_class_or_trait(named) != nullptr) {
                    narrowed[slot] = named;
                }
                continue;
            }

            // §8.8.3 case 2: `has_prop` is "both a runtime check and a
            // narrowing operator". It narrows one property rather than the
            // whole type, so it is recorded separately from the class.
            if (*key == kHasProp) {
                const std::string named = identifier_value(statement);
                if (may_narrow && !named.empty()) {
                    proven[slot].push_back(named);
                }
                continue;
            }

            // A nested combinator inside an object scope.
            if (*key == kOr || *key == kNot || *key == kAnd || *key == kCountAtLeast) {
                const std::optional<stardata::ast::Value> value = statement.value();
                if (const std::optional<Block> inner = value ? value->as_block() : std::nullopt) {
                    Narrowing branch = narrowed;
                    object_scope(slot, *inner, branch, may_narrow && *key != kOr && *key != kNot);
                    if (*key == kAnd) {
                        narrowed = branch; // transparent: the default, spelled out
                    }
                }
                continue;
            }

            if (!is_comparison(statement.op_text())) {
                continue; // a binding, or a predicate this pass has no opinion about
            }
            // §6.6.1: a dotted key is a PATH, and only its first segment
            // names a property of this slot -- `location.exits.north` reads
            // `exits` on the location and then indexes a map. Whether the
            // rest of the path resolves is §6.6's question, not §8.8's, and
            // classifying `exits.north` as a property name would report a
            // property nobody wrote.
            const std::size_t dot = key->find('.');
            report_read(slot, dot == std::string::npos ? *key : key->substr(0, dot), statement,
                        narrowed);
        }
    }

    void report_read(const std::string& slot, const std::string& property,
                     const Statement& statement, const Narrowing& narrowed) {
        const ClassDecl* type = type_of(slot, narrowed, slots, set);
        if (type == nullptr) {
            return; // an unknown slot; not this pass's to report
        }
        const auto proven_here = proven.find(slot);
        if (proven_here != proven.end() &&
            std::find(proven_here->second.begin(), proven_here->second.end(), property) !=
                proven_here->second.end()) {
            return; // §8.8.3 case 2: an explicit has_prop already justified it
        }

        const PropertyLookup lookup =
            stardata::schema::classify_property(property, *type, set, kRootClass);
        if (lookup.answer == PropertyAnswer::Present) {
            return;
        }

        if (lookup.answer == PropertyAnswer::Maybe) {
            Diagnostic diagnostic(Code::PropMaybeAbsent, statement.report_span(),
                                  "'" + slot + "' is a '" + type->id +
                                      "' here, and only some of those have '" + property + "'");
            diagnostic.with_note(list_of(lookup.declared_by) + " declare it (spec §8.8.2)");
            diagnostic.with_note("narrow the slot earlier in this conjunction, or in a stage "
                                 "before it -- `of_class`, `has_trait` and `is` all do, and a "
                                 "restriction that narrows also gives the player a real message "
                                 "(spec §8.8.3)");
            diagnostic.with_fix_it(statement.report_span(), "has_prop = " + property,
                                   "or test it explicitly with `has_prop = " + property + "`");
            sink.report(std::move(diagnostic));
            return;
        }

        Diagnostic diagnostic(Code::PropAbsent, statement.report_span(),
                              "nothing that could be '" + slot + "' here has a '" + property + "'");
        diagnostic.with_note("'" + slot + "' is statically a '" + type->id + "' (spec §8.8.1)");
        if (!lookup.declared_by.empty()) {
            diagnostic.with_note("'" + property + "' is declared by " +
                                 list_of(lookup.declared_by) + ", none of which a '" + type->id +
                                 "' can be");
        }
        // A read that is definitely absent is overwhelmingly a typo, so the
        // useful suggestion is over the properties this slot really has
        // (backlog F6).
        const std::vector<std::string_view> reachable =
            stardata::schema::reachable_properties(*type, set, kRootClass);
        stardata::schema::suggest(
            diagnostic, statement.key() ? statement.key()->span() : statement.report_span(),
            property, reachable);
        sink.report(std::move(diagnostic));
    }

    // A condition block: statements combined with implicit AND (§10.1).
    void conditions(const Block& block, Narrowing& narrowed, bool may_narrow) {
        for (const Statement& statement : block.statements()) {
            const std::optional<std::string> key = statement.key_name();
            const std::optional<stardata::ast::Value> value = statement.value();
            if (!key || key->empty() || !value) {
                continue;
            }
            const std::optional<Block> inner = value->as_block();
            if (!inner) {
                continue;
            }

            if (*key == kOr || *key == kNot || *key == kCountAtLeast) {
                // A barrier. Whatever is learned inside stays inside: only
                // one OR branch is known to have held, and a narrowing under
                // a NOT is a narrowing of the case that did not happen.
                Narrowing branch = narrowed;
                conditions(*inner, branch, /*may_narrow=*/false);
                continue;
            }
            if (*key == kAnd) {
                conditions(*inner, narrowed, may_narrow);
                continue;
            }

            const std::vector<std::string>& slot_list = slot_names();
            if (std::find(slot_list.begin(), slot_list.end(), *key) != slot_list.end()) {
                object_scope(*key, *inner, narrowed, may_narrow);
                continue;
            }
            // Some other predicate that takes a block -- `carrying`,
            // `global`, `script`. Their contents are §10.4's and §10.6's, and
            // this pass has nothing to say about them.
        }
    }

    // `has_prop` narrows a property rather than a type, so it is tracked
    // beside the class narrowing rather than inside it.
    std::map<std::string, std::vector<std::string>> proven;
};

} // namespace

const std::vector<std::string>& slot_names() {
    static const std::vector<std::string> slots = {"actor", "noun",    "second",
                                                   "self",  "speaker", "location"};
    return slots;
}

std::string token_type(std::string_view token) {
    // `[class:weapon]` names its own class, which is the one token that
    // carries a narrower static type (§8.8.1, proposal §6.2).
    constexpr std::string_view prefix = "class:";
    if (token.rfind(prefix, 0) == 0) {
        return std::string(token.substr(prefix.size()));
    }
    // Everything else -- `[something]`, `[things]`, `[something held]`,
    // `[someone]` -- says the slot holds an object and nothing more. See the
    // header on why this is the root and not stdlib's `thing`.
    return std::string(kRootClass);
}

std::vector<Slot> slots_of_action(const Block& action, const SchemaSet& set) {
    std::vector<Slot> slots;

    // §8.8.1's table, for the slots whose type does not depend on the
    // grammar. `actor` and `location` are narrowed by a ruleset rather than
    // by core, so the honest static type for both is the root.
    for (const std::string& name : slot_names()) {
        if (name == "noun" || name == "second") {
            continue;
        }
        // `actor`, `self` and `speaker` are "`person`, or whatever the
        // ruleset narrows it to" (§8.8.1) -- and `person` is stdlib's, so the
        // root is what core can honestly say. There is no spelling yet for a
        // ruleset to declare a slot's static type; see the [OPEN] on F12.
        const bool is_location = name == "location";
        slots.push_back(
            Slot{name, std::string(is_location ? kRoomClass : kRootClass), action.span()});
    }

    // `noun` and `second` come from the action's grammar tokens, in the order
    // they appear on the line.
    const std::optional<stardata::ast::Value> match = action.value_of("match");
    const std::optional<Block> lines = match ? match->as_block() : std::nullopt;
    if (!lines) {
        return slots;
    }

    for (const stardata::ast::Scalar& line : lines->values()) {
        const std::optional<std::string> text = line.as_string();
        if (!text) {
            continue;
        }
        std::vector<std::string> tokens;
        for (std::size_t at = 0; at < text->size();) {
            const std::size_t open = text->find('[', at);
            if (open == std::string::npos) {
                break;
            }
            const std::size_t close = text->find(']', open);
            if (close == std::string::npos) {
                break;
            }
            tokens.push_back(text->substr(open + 1, close - open - 1));
            at = close + 1;
        }

        static const std::vector<std::string> positional = {"noun", "second"};
        for (std::size_t i = 0; i < tokens.size() && i < positional.size(); ++i) {
            const std::string type = token_type(tokens[i]);
            if (set.find_class_or_trait(type) == nullptr) {
                continue;
            }
            const auto existing = std::find_if(slots.begin(), slots.end(), [&](const Slot& slot) {
                return slot.name == positional[i];
            });
            if (existing == slots.end()) {
                slots.push_back(Slot{positional[i], type, line.span()});
            } else if (existing->type_id != type) {
                // Two grammar lines disagreeing about the token: the slot is
                // whichever is broader, and the root always is.
                existing->type_id = std::string(kRootClass);
            }
        }
    }
    return slots;
}

void check_property_reads(const stardata::ast::File& file, const SchemaSet& set,
                          stardata::diag::DiagnosticSink& sink) {
    // A rule has no grammar line of its own: §8.8.1 types `noun` and `second`
    // from "the action's grammar token", and a rule reaches that through
    // `of_action`. So the actions in this file are collected first.
    //
    // ONE FILE ONLY, which is a real limit and worth stating. A rule
    // responding to an action declared elsewhere gets no slot types and is
    // skipped rather than guessed at; carrying declarations across files is
    // the two-pass load of backlog F9.
    std::map<std::string, std::vector<Slot>> by_action;
    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        const std::optional<stardata::ast::Value> value = statement.value();
        const std::optional<Block> block = value ? value->as_block() : std::nullopt;
        if (!key || *key != "action" || !block) {
            continue;
        }
        const std::optional<stardata::ast::Value> id = block->value_of("id");
        const std::optional<stardata::ast::Scalar> scalar = id ? id->as_scalar() : std::nullopt;
        if (const std::optional<std::string_view> name =
                scalar ? scalar->as_identifier() : std::nullopt) {
            by_action.emplace(std::string(*name), slots_of_action(*block, set));
        }
    }

    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key || key->empty()) {
            continue;
        }
        const stardata::schema::Schema* schema = set.find(*key);
        if (schema == nullptr || schema->stage_order.empty()) {
            continue; // not a staged form; nothing to flow a narrowing through
        }
        const std::optional<stardata::ast::Value> value = statement.value();
        const std::optional<Block> block = value ? value->as_block() : std::nullopt;
        if (!block) {
            continue;
        }

        // The form's own grammar if it has one; otherwise the grammar of
        // the action it responds to.
        std::vector<Slot> slots = slots_of_action(*block, set);
        if (const std::optional<stardata::ast::Value> of_action = block->value_of(kOfAction)) {
            const std::optional<stardata::ast::Scalar> named = of_action->as_scalar();
            const std::optional<std::string_view> id =
                named ? named->as_identifier() : std::nullopt;
            const auto found = id ? by_action.find(std::string(*id)) : by_action.end();
            if (found != by_action.end()) {
                for (const Slot& slot : found->second) {
                    if (slot.name == "noun" || slot.name == "second") {
                        slots.push_back(slot);
                    }
                }
            }
        }
        Walk walk{set, slots, sink, {}};

        // §8.8.3: a narrowing in one stage narrows every later one. The
        // sequence comes from the schema, so this loop is the whole of what
        // this library knows about stages -- which is that they are ordered.
        Narrowing narrowed;
        for (const std::string& stage : schema->stage_order) {
            const std::optional<stardata::ast::Value> stage_value = block->value_of(stage);
            const std::optional<Block> stage_block =
                stage_value ? stage_value->as_block() : std::nullopt;
            if (!stage_block) {
                continue;
            }
            walk.conditions(*stage_block, narrowed, /*may_narrow=*/true);
        }
    }
}

} // namespace starcore
