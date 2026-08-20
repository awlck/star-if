// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/loader.hpp"

namespace starcore {

// Where a `failureMsg` may go, and where one is owed (spec §10.5, backlog F8).
//
// The rule is subtle and entirely mechanical, which is the combination worth
// enforcing: an author gets it wrong by writing a perfectly well-formed
// message in a position where the engine will never print it, and nothing
// about the file looks wrong afterwards. §10.5's own opening is the reason it
// matters -- a restriction that fails without a message drops the engine onto
// its generic fallback, "which is the single most common way a parser game
// feels unfinished".
//
// WHICH BLOCKS ARE CONDITION BLOCKS COMES FROM THE SCHEMA, not from a list
// here: a key's declared type is `condition_block` (§6.2) or it is not. So
// `restrictions`, `conditions` and `when` are found by asking the registry,
// and a ruleset that declares a form with a condition stage of its own gets
// the reachability check for free.
//
// WHICH OF THEM ARE SILENT IS VOCABULARY, and is named below. §10.5 says
// "`failureMsg` MUST NOT appear in a `conditions` or `when` block" and names
// exactly those two, so this names exactly those two. A condition stage that
// is neither `restrictions` nor one of them is left unclassified and gets
// only the reachability check -- silence about a stage this library has never
// heard of is better than guessing which way it goes, since guessing "silent"
// would turn a correct message into an error.

// §10.5's restriction context: the stage whose failure the player is owed an
// explanation for, because by then the action has definitely been attempted.
[[nodiscard]] std::string_view restriction_stage() noexcept;

// The stages §10.5 names as silent by definition.
[[nodiscard]] const std::vector<std::string>& silent_stages();

// §10.5's key. Not a schema key inside a condition block -- the contents of
// one are §10's, and no schema describes them -- so the name is stated here.
[[nodiscard]] std::string_view failure_message_key() noexcept;

// §10.5 and §10.5.1 over every declaration in a file.
//
// Reports:
//
//   E-FAILMSG-SILENT       a message in a `conditions` or `when` block, which
//                          the player can never be shown (§10.5).
//   E-FAILMSG-UNREACHABLE  a message below a `NOT`, `OR` or `COUNT_AT_LEAST`,
//                          which fail as a whole so no child explains them
//                          (§10.5.1). Cites the barrier as well as the message.
//   W-FAILMSG-MISSING      a non-empty `restrictions` block with no reachable
//                          message and none on the enclosing declaration
//                          (§10.5.3). A warning, not an error, because steps
//                          3 and 4 of the fallback chain are what make the
//                          spec's own word SHOULD rather than MUST.
//
// A separate pass over the tree, like `check_placements` and
// `check_property_reads`: `stardata` loads and validates, then whoever owns
// the vocabulary walks the result.
void check_failure_messages(const stardata::ast::File& file, const stardata::schema::SchemaSet& set,
                            stardata::diag::DiagnosticSink& sink);

} // namespace starcore
