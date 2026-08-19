// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::schema {

// "Did you mean ...?" (backlog F6).
//
// §7.3 asks for this by name -- "an unknown key MUST be an error, with a
// 'did you mean ...?' suggestion computed by edit distance against the
// declared keys" -- and §14.3 asks for it again on two more rows. It is the
// half of the schema layer an author actually experiences: proposal §4.9's
// worked example is an `outdoor_room` block that should have said
// `outdoors_room`, which under a Clausewitz-style loader is a silently
// ignored block and a room that mysteriously does not exist.
//
// WHY A SEPARATE FILE. Four passes need it -- the key checker, the top-level
// form checker, the type checker and the registry -- and they are in three
// translation units. More to the point, the distance function is the sort of
// thing that gets quietly reimplemented slightly differently in each place,
// and then two diagnostics disagree about whether `nrth` resembles `north`.

// Damerau-Levenshtein distance (insertion, deletion, substitution, and the
// transposition of two adjacent characters), capped at `limit`.
//
// Transposition is in because `sucessMsg` and `successMsg` differ by a
// deletion but `msesage` and `message` differ by a swap, and a plain
// Levenshtein charges two edits for a swap -- enough to push a one-finger
// slip past the threshold on a short name.
//
// Returns `limit + 1` for anything further apart, which lets the search stop
// early rather than finish computing a distance nobody will use. Compares
// bytes, not code points: every identifier in the format is ASCII (§3.3), and
// a string key that is not still gets a usable answer.
[[nodiscard]] std::size_t edit_distance(std::string_view a, std::string_view b,
                                        std::size_t limit) noexcept;

// How far a name of this length may be from a candidate and still be a
// plausible typo of it: a third of the longer of the two, never less than 1.
//
// A third is the conventional bar and errs toward being helpful. The cost of
// a wrong suggestion is that an author reads one extra word and ignores it;
// the cost of no suggestion where an obvious one existed is the failure §7.3
// exists to prevent, so the two are not symmetric.
[[nodiscard]] std::size_t distance_limit(std::string_view written,
                                         std::string_view candidate) noexcept;

// The nearest candidate to `written`, or nothing when none is near enough.
//
// DETERMINISTIC, which §14.1 requires of everything here and which a "did
// you mean" is unusually prone to violating. Candidates are considered in the
// order given -- declaration order, which is load order (§13.2) -- and the
// first of equally-near candidates wins. Not the alphabetically first, and
// not whichever a hash table produced: an author comparing two builds must
// get the same advice from both.
//
// A candidate differing only in case wins outright, whatever the distance.
// `SUCCESSMSG` is nine edits from `successMsg` and is obviously meant to be
// it, and this is the one case where distance is the wrong measure.
[[nodiscard]] std::optional<std::string_view>
nearest(std::string_view written, const std::vector<std::string_view>& candidates);

// `nearest`, attached to a diagnostic as a fix-it over `at`. Returns whether
// there was one, so a caller can word the rest of the diagnostic differently
// when there is not.
bool suggest(diag::Diagnostic& diagnostic, diag::Span at, std::string_view written,
             const std::vector<std::string_view>& candidates);

} // namespace stardata::schema
