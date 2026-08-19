// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/loader.hpp"
#include "stardata/schema/property.hpp"

namespace starcore {

// Reading a property the object may not have (spec §8.8, backlog F12).
//
// The one genuinely novel piece of static analysis in Phase 0, and the task
// proposal §2.1.1 says "most threatened the layering". It is split, and this
// header is the vocabulary half:
//
//   `libs/stardata` answers "does this class declare that property?" -- a
//   question about the class graph, with no interactive fiction in it. That
//   is `schema/property.hpp`'s `classify_property`, and it names nothing.
//
//   `libs/starcore` answers "what is `noun`, which keys narrow it, and which
//   escape the check?" -- §10.2's slots and §10.4's predicates, which are
//   vocabulary to their bones. That is this file.
//
// THE STAGE ORDER IS THE EXCEPTION, and it is what makes the split work at
// all. §8.8.3 has narrowing flow forward from `when` to `conditions` to
// `restrictions` to `effects`; hard-coding that sequence anywhere would be a
// fourth piece of vocabulary. Instead the schema declares `stage_order`
// (§7.2) and the walk below reads it, so even this library does not contain
// the list -- a ruleset that adds a stage gets narrowing through it by
// declaring it, and `libs/stardata`, which runs no part of this, never had to
// know the stages existed.

// §10.2's object slots: "a key naming an object slot (`actor`, `noun`,
// `second`, `self`, `speaker`, `location`) or an object id opens a block of
// conditions about that object".
//
// Listed here, in core, because that is what proposal §2.1.1 says core is
// for. The
// static type of each comes from §8.8.1's table, except `noun` and `second`,
// which come from the action's grammar token.
[[nodiscard]] const std::vector<std::string>& slot_names();

// The static type of a slot, as an id naming a class (§8.8.1).
struct Slot {
    std::string name;
    std::string type_id; // the class the slot is statically known to hold
    stardata::diag::Span span;
};

// The static type `[something]`, `[class:weapon]` and the rest yield
// (proposal §6.2, spec §8.8.1).
//
// `[something]` yields the ROOT CLASS, not stdlib's `thing`. §8.8.1 writes
// "`[something]` yields `thing`" and that is true of a stdlib game, but
// `thing` is stdlib's and core may not depend on it -- §7.2.4 is explicit
// that everything in stdlib "could be replaced wholesale by a different
// library". The root is `starcore.object`, which core does own, and which is
// the honest answer: a token matching "one object in scope" tells you the
// thing is an object and nothing more. The consequence is that a broad token
// gives broad knowledge, which is exactly §8.8.1's advice read forwards --
// narrow with a restriction, not with the grammar.
[[nodiscard]] std::string token_type(std::string_view token);

// The slots an action's `match` lines establish, with their static types.
[[nodiscard]] std::vector<Slot> slots_of_action(const stardata::ast::Block& action,
                                                const stardata::schema::SchemaSet& set);

// §8.8's analysis over every declaration in a file whose schema declares a
// `stage_order`.
//
// Reports E-PROP-ABSENT for a read that nothing satisfying the slot could
// answer, and E-PROP-MAYBE-ABSENT for one that some objects could and others
// could not, with a `has_prop` fix-it. Narrowing from `of_class`, `has_trait`,
// `is` and `has_prop` flows forward through the stages; it does not escape an
// `OR` or a `NOT`.
//
// A separate pass over the tree, like `check_placements`, for the same
// reason: `stardata` loads and validates, then whoever owns the vocabulary
// walks the result.
void check_property_reads(const stardata::ast::File& file, const stardata::schema::SchemaSet& set,
                          stardata::diag::DiagnosticSink& sink);

} // namespace starcore
