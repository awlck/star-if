// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/loader.hpp"

namespace starcore {

// The permitted-key rule (spec §7.4, §8.7; backlog F11's first `[OPEN]`).
//
// §7.4: "The keys permitted inside an instantiation block are those of the
// class's property set (§8), plus the universal keys `id`, `traits`, `in`,
// `on`, `part_of`, and `sector`." §6.2's declared-type check (F4) already
// covers a key that DOES resolve to a property; what it deliberately leaves
// alone is a key that resolves to NOTHING, because telling a typo apart from
// a universal key needs the universal-key list, and four of the six words in
// it are core vocabulary -- `in`, `on` and `part_of` are values of the
// relation enum (§8.5), and `sector` is a core-owned form. `libs/stardata`
// may not name them (proposal §2.1.1), so it stops one question short of
// §8.7's own stated purpose: "`times_reboofed` is an error rather than a
// second property."
//
// WHY THIS IS HERE AND NOT IN `stardata`, and why it reuses `placement.hpp`'s
// machinery rather than growing its own copy of the universal-key list: this
// is the same pass, in the sense that matters -- it walks the same
// instantiation blocks placement.cpp already walks, for the same reason
// (§7.4's own vocabulary is core's), and `holder` and `relation` need no
// special case here because F13 made them ordinary properties of
// `starcore.object`, reachable through the same class walk as everything
// else. Only the sugar keywords (`in`, `on`, ...), `id`, `traits` and
// `prop_def` are not properties at all, and those are exactly what this file
// adds to the skip list `check_instantiation` already leaves for someone else
// (see the note on `check_instantiation` in schema/types.hpp).
//
// F6'S SUGGESTIONS ARE NOT A SEPARATE TASK. `schema/property.hpp` already
// exposes `reachable_properties` for exactly this call site ("the caller that
// wants it is in the other library"), and `schema/suggest.hpp` already
// implements the "did you mean" machinery every other unknown-name diagnostic
// in this codebase uses. Reporting one without the other would be new
// asymmetry, not new work.
void check_instance_properties(const stardata::ast::File& file,
                               const stardata::schema::SchemaSet& set,
                               stardata::diag::DiagnosticSink& sink);

} // namespace starcore
