// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/schema/loader.hpp"

namespace starcore {

// Globals, constants and flags (spec §6.4 and §6.4.1, backlog F10).
//
// §6.4's rule is one sentence -- "Both MUST be declared. There is no implicit
// creation." -- and §6.4.1 says why it is worth the words: as undeclared
// magic strings, `set_flag = captain_found` paired with `flag_set =
// captain_finded` is a silent, permanent bug. The condition never fires and
// nothing reports it. Requiring the declaration turns the typo into a
// compile error, which is decision A17.
//
// WHAT IS CORE'S HERE AND WHAT IS NOT. The `global` and `const` *forms* are
// format forms (§7.2.4): `libs/stardata` parses them, registers each
// declaration in the `SchemaSet` and checks the declared type and the initial
// value. This pass never reads a declaration -- it asks the registry.
//
// What it does own is every way a global is *named*: `set_flag`,
// `clear_flag`, `flag_set` (§6.4.1) and `set_global`, `add_global` (§11.1)
// are the condition and effect vocabularies of §10 and §11, and a library
// that knew those words would be a schema layer with opinions about
// interactive fiction. So the format layer answers "what is declared" and
// this answers "who names it, and did they mean it".
//
// TWO PHASES, because §13.2 lets a global be declared in one file and read in
// another, in either order. The shape `TextIndex` has, for the same reason.

class GlobalIndex {
public:
    // Reads one file: every use of a global in it. The declarations
    // themselves are skipped -- the format layer has them, and walking one
    // would count its own `id = X` as a mention of itself, which would make
    // every declared global look read.
    void add_file(const stardata::ast::File& file);

    // Reports what only the whole picture can decide, against the globals the
    // registry holds.
    //
    //   E-FLAG-UNDECLARED    `set_flag` / `clear_flag` / `flag_set` naming no
    //                        declared global (§6.4.1), with a suggestion.
    //   E-FLAG-NOT-BOOL      ...naming one that is not `bool`.
    //   E-GLOBAL-UNDECLARED  `set_global` / `add_global` / a `global = { … }`
    //                        condition naming no declared global (§6.4).
    //   W-GLOBAL-UNUSED      a declared global or const nothing reads.
    void check(const stardata::schema::SchemaSet& set, stardata::diag::DiagnosticSink& sink) const;

private:
    // One reference to a global, resolved in `check` rather than where it was
    // found. The kind decides three separate questions -- is this a read, is
    // it held to §6.4.1's bool rule, and is the site certain enough to report
    // an undeclared name at -- so it is one enum rather than three flags that
    // can be set in combinations none of the sites produce.
    enum class Kind : std::uint8_t {
        FlagWrite, // `set_flag = X` / `clear_flag = X`
        FlagRead,  // `flag_set = X`
        Write,     // `set_global = { id = X … }` / `add_global`
        Read,      // a key of a `global = { X == v }` condition block
        Mentioned, // an identifier that merely matches a declared id
    };

    struct Use {
        std::string id;
        stardata::diag::Span span;
        Kind kind = Kind::Mentioned;

        // §6.4.1 applies to the sugar and to nothing else.
        [[nodiscard]] bool flag_sugar() const noexcept {
            return kind == Kind::FlagWrite || kind == Kind::FlagRead;
        }

        // A write alone does not stop a global being unused; that is the
        // whole point of §14.3's "never read".
        [[nodiscard]] bool reads() const noexcept {
            return kind == Kind::FlagRead || kind == Kind::Read || kind == Kind::Mentioned;
        }

        // Whether this site names a global for certain. Everything but
        // `Mentioned` does; `Mentioned` is the inference §6.6's datum
        // resolution would make exact, and until then it may not carry an
        // error.
        [[nodiscard]] bool certain() const noexcept { return kind != Kind::Mentioned; }
    };

    void walk(const stardata::ast::Block& block);
    void note(std::string id, stardata::diag::Span span, Kind kind);

    std::vector<Use> uses_;
};

} // namespace starcore
