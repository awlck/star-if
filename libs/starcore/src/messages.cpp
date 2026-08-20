// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/messages.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"

#include "starcore/conditions.hpp"

namespace starcore {
namespace {

using stardata::ast::Block;
using stardata::ast::Statement;
using stardata::ast::Value;
using stardata::diag::Code;
using stardata::diag::Diagnostic;
using stardata::diag::DiagnosticSink;
using stardata::diag::Span;
using stardata::schema::KeyDecl;
using stardata::schema::Schema;
using stardata::schema::SchemaSet;

constexpr std::string_view kRestrictions = "restrictions";
constexpr std::string_view kConditions = "conditions";
constexpr std::string_view kWhen = "when";
constexpr std::string_view kFailureMsg = "failureMsg";

// §6.2's type for a block of conditions. Stardata's vocabulary rather than
// this library's, which is exactly why the pass can use it to find the
// stages instead of listing them.
constexpr std::string_view kConditionBlock = "condition_block";

// What §10.5 says about one condition stage.
enum class Stage {
    Restriction, // owes the player a message when it fails
    Silent,      // decided before the action was attempted; a message is never shown
    Unclassified // some other form's condition stage: reachability only
};

[[nodiscard]] Stage stage_of(std::string_view key) noexcept {
    if (key == kRestrictions) {
        return Stage::Restriction;
    }
    for (const std::string& silent : silent_stages()) {
        if (key == silent) {
            return Stage::Silent;
        }
    }
    return Stage::Unclassified;
}

// "a NOT", but "an OR". Two words of care, in the one message an author who
// has just made this mistake is going to read closely.
[[nodiscard]] std::string with_article(std::string_view barrier) {
    return (barrier == combinator::kOr ? "an " : "a ") + std::string(barrier);
}

// Why a message under this combinator can never be printed. One sentence per
// barrier, because the three fail for three different reasons and an author
// who has just been told "unreachable" wants to know which.
[[nodiscard]] std::string why_unreachable(std::string_view barrier) {
    if (barrier == combinator::kNot) {
        return "a NOT fails when its contents SUCCEED, so the NOT is what failed -- a message "
               "inside it describes the state that did hold (spec §10.5.1)";
    }
    if (barrier == combinator::kOr) {
        return "an OR fails only when every branch fails, so no single branch is the reason and "
               "none of them can explain it (spec §10.5.1)";
    }
    return "a COUNT_AT_LEAST fails when too few of its children hold, so no single child is the "
           "reason it failed (spec §10.5.1)";
}

// §10.5.1's reachability, as the specification states it: a message is
// reachable if its block can be reached from the root of the condition block
// "by descending through conjunction edges only -- that is, through plain
// blocks and explicit AND blocks. The NOT / OR / COUNT_AT_LEAST node at such
// a position is itself reachable and carries its own message; everything
// below it is not."
//
// `owner` is the key of `block` itself, and it is the whole subtlety of this
// function -- a barrier's OWN message is the reachable one, so the barrier
// takes effect on the way *out* of its block rather than on the way in. The
// first draft of this file tested the barrier on the way in and reported
// every correct `NOT = { ... failureMsg = ... }` in the reference corpus as
// an error, which is the opposite of the mistake §10.5.1 exists to catch.
[[nodiscard]] bool has_reachable_message(const Block& block, std::string_view owner) {
    for (const Statement& statement : block.statements()) {
        if (statement.key_name() == kFailureMsg) {
            return true;
        }
    }
    if (is_barrier(owner)) {
        return false; // its own direct message was the case above; its children's do not count
    }
    for (const Statement& statement : block.statements()) {
        const std::optional<std::string> key = statement.key_name();
        const std::optional<Value> value = statement.value();
        const std::optional<Block> inner = value ? value->as_block() : std::nullopt;
        if (key && inner && has_reachable_message(*inner, *key)) {
            return true;
        }
    }
    return false;
}

class Pass {
public:
    Pass(const SchemaSet& set, DiagnosticSink& sink) noexcept : set_(set), sink_(sink) {}

    // One declaration -- a top-level form, or a nested `block<S>` such as the
    // `rule` inside an action. `supplied` is §10.5.3's steps 3 and 4: a
    // `failureMsg` on this declaration or on one enclosing it covers every
    // restriction below, which is what makes the missing-message report a
    // warning about a whole action rather than about one line.
    void declaration(const Block& block, const Schema& schema, bool supplied) {
        const bool covers = supplied || block.find(kFailureMsg).has_value();

        for (const Statement& statement : block.statements()) {
            const std::optional<std::string> key = statement.key_name();
            const std::optional<Value> value = statement.value();
            const std::optional<Block> inner = value ? value->as_block() : std::nullopt;
            if (!key || !inner) {
                continue;
            }
            const KeyDecl* declared = schema.find_key(*key);
            if (declared == nullptr) {
                continue; // an open schema's extra key; no declared shape to walk
            }

            if (declared->type.name == kConditionBlock) {
                condition_stage(statement, *inner, *key, covers);
                continue;
            }

            // `block<S>` -- the `rule` inside an `action`, and any other
            // nested declaration a form declares. §10.5.3 walks up through
            // these, so `covers` travels down through them.
            if (declared->type.name == "block" && !declared->type.args.empty()) {
                if (const Schema* nested = set_.find(declared->type.args[0].name)) {
                    declaration(*inner, *nested, covers);
                }
            }
        }
    }

private:
    void condition_stage(const Statement& statement, const Block& block, std::string_view key,
                         bool covers) {
        const Stage stage = stage_of(key);
        const bool any_message = scan(block, stage == Stage::Silent ? key : std::string_view{}, key,
                                      statement.report_span(), {}, {});

        if (stage != Stage::Restriction || covers) {
            return;
        }
        // A message that is present but unreachable has already been
        // reported, and "nothing here explains a refusal" is not true of it:
        // the author wrote the explanation, in the wrong place. Moving it
        // answers both, so saying both is one diagnostic too many.
        if (any_message) {
            return;
        }
        // §10.5's SHOULD. An empty `restrictions` block is §5.4.2's explicit
        // override -- "this action has no restrictions" -- and has nothing to
        // explain, so it is not a missing message.
        if (block.is_empty() || has_reachable_message(block, key)) {
            return;
        }
        Diagnostic diagnostic(Code::FailmsgMissing, statement.report_span(),
                              "nothing here explains a refusal, so the player is told only "
                              "that they can't do that right now");
        diagnostic.with_note("a restriction that fails without a message drops the engine onto "
                             "its generic fallback, which is the single most common way a parser "
                             "game feels unfinished (spec §10.5)");
        diagnostic.with_note("one `failureMsg` on the enclosing action or rule covers every "
                             "restriction in it, so this need not be answered line by line "
                             "(spec §10.5.3)");
        sink_.report(std::move(diagnostic));
    }

    // Walks a condition block looking for messages.
    //
    // `silent` is the stage's name when the stage is silent. `owner` is the
    // key of `block` itself and `owner_span` where it was written; `barrier`
    // is the combinator already descended through, or empty.
    //
    // THE BARRIER IS ESTABLISHED ON THE WAY OUT, not on the way in, and that
    // is the only hard part of this pass. §10.5.1 makes a barrier's own
    // message the reachable one -- `NOT = { carrying = { ... } failureMsg =
    // ... }` is the form §10.5's example uses and every correct action in the
    // corpus -- so `barrier` is what stands between `block` and the root, and
    // a barrier at `owner` only starts applying to `block`'s children.
    // Returns whether the block held a `failureMsg` anywhere at all, which
    // the caller needs in order not to also report one as missing.
    [[nodiscard]] bool scan(const Block& block, std::string_view silent, std::string_view owner,
                            std::optional<Span> owner_span, std::string_view barrier,
                            std::optional<Span> barrier_span) {
        // An inner barrier does not replace an outer one -- once unreachable,
        // always unreachable -- so the outermost is the one an author is told
        // about, since moving the message there is the fix.
        std::string_view inner_barrier = barrier;
        std::optional<Span> inner_span = barrier_span;
        if (inner_barrier.empty() && is_barrier(owner)) {
            inner_barrier = owner;
            inner_span = owner_span;
        }

        bool any_message = false;
        for (const Statement& statement : block.statements()) {
            const std::optional<std::string> key = statement.key_name();
            if (!key) {
                continue;
            }

            if (*key == kFailureMsg) {
                any_message = true;
                if (!silent.empty()) {
                    report_silent(statement, silent);
                } else if (!barrier.empty()) {
                    report_unreachable(statement, barrier, barrier_span);
                }
                continue;
            }

            const std::optional<Value> value = statement.value();
            const std::optional<Block> inner = value ? value->as_block() : std::nullopt;
            if (!inner) {
                continue;
            }
            any_message |=
                scan(*inner, silent, *key, statement.report_span(), inner_barrier, inner_span);
        }
        return any_message;
    }

    void report_silent(const Statement& statement, std::string_view stage) {
        Diagnostic diagnostic(Code::FailmsgSilent, statement.report_span(),
                              "a '" + std::string(stage) +
                                  "' block is silent, so this message can never be shown");
        diagnostic.with_note("`conditions` and `when` decide whether an action applies at all, "
                             "before it has been attempted -- failing one is not a refusal the "
                             "player is owed an explanation for (spec §10.5)");
        diagnostic.with_note("a message the player should see belongs in `restrictions`, where "
                             "failing means the attempt was made and turned down");
        sink_.report(std::move(diagnostic));
    }

    void report_unreachable(const Statement& statement, std::string_view barrier,
                            const std::optional<Span>& barrier_span) {
        Diagnostic diagnostic(Code::FailmsgUnreachable, statement.report_span(),
                              "this message sits below " + with_article(barrier) +
                                  ", which fails as a whole, so it can never be shown");
        diagnostic.with_note(why_unreachable(barrier), barrier_span);
        diagnostic.with_note("move it onto the " + std::string(barrier) +
                             " itself, which is the innermost block whose own failure is the "
                             "reason the restriction failed (spec §10.5.1)");
        sink_.report(std::move(diagnostic));
    }

    const SchemaSet& set_;
    DiagnosticSink& sink_;
};

} // namespace

std::string_view restriction_stage() noexcept {
    return kRestrictions;
}

const std::vector<std::string>& silent_stages() {
    static const std::vector<std::string> stages = {std::string(kConditions), std::string(kWhen)};
    return stages;
}

std::string_view failure_message_key() noexcept {
    return kFailureMsg;
}

void check_failure_messages(const stardata::ast::File& file, const SchemaSet& set,
                            DiagnosticSink& sink) {
    Pass pass(set, sink);
    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key) {
            continue;
        }
        const Schema* schema = set.find(*key);
        const std::optional<Value> value = statement.value();
        const std::optional<Block> block = value ? value->as_block() : std::nullopt;
        if (schema == nullptr || !block) {
            continue; // an object instantiation, or a form nothing declares
        }
        pass.declaration(*block, *schema, /*supplied=*/false);
    }
}

} // namespace starcore
