// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"

namespace stardata::schema {

struct KeyDecl;

// Annotations and combination modes (backlog F5, spec §5.4).
//
// An annotation says what a value does to the value it would otherwise have
// inherited. §5.4.1 defines ten of them and §5.4.2 says what happens when a
// value carries none -- which is the common case, and is where the schema's
// `combine` mode comes in.
//
// THE VOCABULARY IS THE SPECIFICATION'S, so it is named here in C++ rather
// than read out of a `.star` file. That is the same call §6.2's type names
// got in schema/types.cpp and for the same reason: §1.2.1 puts §2-§7 in
// Stardata's column, so `@before` is no more a library's to redefine than
// `int` is. Nothing here is IF vocabulary, and the layering check of
// proposal §2.1.1 agrees -- none of these names appears in
// libs/starcore/builtin/.
//
// TWO OF THEM ARE NOT COMBINATION AT ALL. `@debug` and `@platform` decide
// whether the statement is there, and -- the part that is easy to get wrong,
// and that §5.4.1 used to state as though it were one rule -- they decide it
// at two different times. `@debug` is resolved by the compiler; `@platform`
// cannot be, because one compiled game runs under every frontend and the
// frontend declares itself at session start. `Presence` below is that
// distinction, and it is what the duplicate-key check consults.

// The ten annotations of §5.4.1. An eleventh spelling is an error: §3.8
// requires it, because silently ignoring one changes what a value means with
// nothing on screen to say so.
enum class AnnotationKind : std::uint8_t {
    Before,
    After,
    Override,
    Merge,
    Remove,
    Priority,
    Debug,
    Platform,
    Style,
    Replaces,
};

[[nodiscard]] std::string_view to_string(AnnotationKind kind) noexcept;
[[nodiscard]] std::optional<AnnotationKind> annotation_from_string(std::string_view name) noexcept;

// Whether `name` is one of the frontend ids §5.4.1 lists for `@platform`.
[[nodiscard]] bool is_frontend(std::string_view name) noexcept;

// What a value does to the value it inherits, once §5.4.1's annotations and
// §5.4.2's defaults have both had their say.
//
// Phase 0 computes this and checks it; *performing* the combination is the
// compiler's, since there is nothing to combine until a load order has been
// resolved (§13.2). Exposing it now is what makes the schema's `combine`
// declaration mean something a reader can check rather than a word the
// loader stores and nobody consults.
struct Combination {
    // The five combining annotations. `@priority` is not among them: it
    // orders values within a phase rather than choosing one, so it travels
    // alongside a mode instead of being one.
    enum class Mode : std::uint8_t { Before, After, Override, Merge, Remove };

    Mode mode = Mode::Override;
    std::int64_t priority = 0;

    // Whether the mode was written on the value. False means it came from
    // §5.4.2's table -- which is not a lesser fact, but is a different one:
    // an editor rewriting the statement must not invent an annotation the
    // author did not put there.
    bool annotated = false;

    // The annotation, when there is one; otherwise the statement's key.
    diag::Span span;
};

[[nodiscard]] std::string_view to_string(Combination::Mode mode) noexcept;

// §5.4.1 and §5.4.2 together, for one statement. `declared` is the key's
// declaration, which supplies the default; a null one means the key has no
// declaration to read a `combine` from -- an open schema's extra key, or a
// property inside an instantiation -- and §5.4.2's first row applies, since
// `combine` defaults to `override`.
//
// `smart` is the row that needs the value as well as the schema: it is
// `@override` for an empty block and `@after` otherwise, which is exactly
// the rule proposal §7.2 stated behaviourally for the rule sub-blocks and
// §5.4.2 named.
[[nodiscard]] Combination combination_of(const ast::Statement& statement, const KeyDecl* declared);

// When a statement is there (§5.4.1's conditional-presence annotations).
//
// Two axes, resolved at two different times, which is the whole content of
// this structure. Lumping them was the specification's own first reading and
// it does not survive contact with the distribution model:
//
//   * `@debug` is COMPILE TIME. The compiler strips it from a release build,
//     leaving a `.spak` that never contained it.
//   * `@platform` is RUN TIME. One `.spak` is signed and shipped for every
//     frontend (proposal §14.2 and proposal §14.4), and which frontend is
//     running is not known until it declares its capabilities at session
//     start (proposal §12.2). So the compiler MUST keep every alternative
//     and the engine selects among them. A compiler that stripped
//     `@platform` would be committing the author to one artefact per
//     frontend.
//
// Not a boolean, because the question a checker can answer is never "is this
// present?" -- there is no one build, and no one frontend, to ask about --
// but "could these two bind the same key at once?".
struct Presence {
    // `@debug`: the statement is in a development build and not a release
    // one. Nothing below the compiler acts on this; it is here so the
    // duplicate-key check knows the statement is not conditional in the way
    // `@platform` is.
    bool debug_only = false;

    // `@platform`: the frontends the statement is selected on, in source
    // order. Empty means every frontend, which is what a statement carrying
    // no `@platform` says.
    std::vector<std::string> frontends;

    // The annotation that made it conditional, for a diagnostic to point at.
    // Absent when the statement is unconditional.
    std::optional<diag::Span> span;

    [[nodiscard]] bool unconditional() const noexcept { return !debug_only && frontends.empty(); }

    // Whether the compiler strips this statement from a release build.
    [[nodiscard]] bool stripped_from_release() const noexcept { return debug_only; }

    // Whether the engine, not the compiler, chooses whether this statement
    // applies. Starforge reads this to know what it may not resolve: a true
    // here is an alternative that has to survive into the `.spak`.
    [[nodiscard]] bool selected_at_runtime() const noexcept { return !frontends.empty(); }

    // Whether both bind the same key at once somewhere -- in some build, on
    // some frontend. §5.4.1's arity rule, made pairwise, and deliberately
    // asymmetric between the two annotations:
    //
    //   * `@platform` PARTITIONS, when the frontend sets are disjoint. Both
    //     alternatives ship, and the engine picks exactly one per session,
    //     so neither is a duplicate of the other. Overlapping sets are: on
    //     the shared frontend the engine would hold two candidates and have
    //     no rule to choose between them.
    //   * `@debug` DOES NOT PARTITION ANYTHING. A development build has the
    //     `@debug` statement *and* the plain one, so those two collide --
    //     and so do two `@debug` statements. `@debug` narrows which builds a
    //     statement reaches without ever excluding another statement from
    //     those builds.
    //
    // A statement with no `@platform` runs on every frontend and therefore
    // overlaps every gated one. §5.4.1 declines to make that pair a default
    // and an exception: there is no fallback precedence, so an author who
    // wants one writes the general case as its own alternative.
    [[nodiscard]] bool can_coexist_with(const Presence& other) const;
};

[[nodiscard]] Presence presence_of(const ast::Statement& statement);

// Checks every annotation in a file: that it is one of §5.4.1's, that its
// arguments are what §3.8 and §5.4.1 say, that it applies to the shape of
// value it is written on, and that no value combines two ways at once.
//
// Walks the whole tree rather than the top level, because an annotation is
// legal on any value and an unknown one four blocks down is exactly as
// invisible as an unknown one at the top -- more so, in fact, since nobody
// is looking there.
//
// Registry-free by construction: every question above is answered by §3.8
// and §5.4.1 alone. The one annotation argument that names something
// declared is `@style(id)`, and whether that style exists is E-STYLE-
// UNDECLARED, which is backlog F7's.
void check_annotations(const ast::File& file, diag::DiagnosticSink& sink);

} // namespace stardata::schema
