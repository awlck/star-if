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
// WHY THIS IS `starcore` AND NOT THE SCHEMA LAYER. §6.4 sits in Stardata's
// half of §1.2.1, but everything this pass has to name is core's: `global`
// and `const` are core-owned forms (§7.2.4, "save-state layout"), and
// `set_flag`, `clear_flag`, `flag_set`, `set_global` and `add_global` are the
// condition and effect vocabularies of §10 and §11. The schema layer already
// does the part that is mechanism -- one namespace, one declaration per id,
// enforced by `unique_in = global` -- and this is the rest.
//
// TWO PHASES, because §13.2 lets a global be declared in one file and read in
// another, in either order. The shape `TextIndex` has, for the same reason.

class GlobalIndex {
public:
    struct Declaration {
        std::string id;
        stardata::ast::TypeRef type;
        bool is_const = false;
        stardata::diag::Span span;

        [[nodiscard]] bool is_bool() const noexcept { return type.name == "bool"; }
    };

    // Reads one file: the globals and constants it declares, and every use of
    // one. Checks each declared type against §6.2 (through the schema layer's
    // `check_type`) and each initial value against the type declared beside
    // it -- which is the check no `type =` on a key declaration can express,
    // and the reason those keys are typed `any`.
    void add_file(const stardata::ast::File& file, const stardata::schema::SchemaSet& set,
                  stardata::diag::DiagnosticSink& sink);

    // Reports what only the whole picture can decide.
    //
    //   E-FLAG-UNDECLARED    `set_flag` / `clear_flag` / `flag_set` naming no
    //                        declared global (§6.4.1), with a suggestion.
    //   E-FLAG-NOT-BOOL      ...naming one that is not `bool`.
    //   E-GLOBAL-UNDECLARED  `set_global` / `add_global` / a `global = { … }`
    //                        condition naming no declared global (§6.4).
    //   W-GLOBAL-UNUSED      a declared global or const nothing reads.
    void check(stardata::diag::DiagnosticSink& sink) const;

    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept {
        return declarations_;
    }
    [[nodiscard]] const Declaration* find(std::string_view id) const noexcept;

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

    void read_declaration(const stardata::ast::Block& block, bool is_const,
                          const stardata::schema::SchemaSet& set,
                          stardata::diag::DiagnosticSink& sink);
    void walk(const stardata::ast::Block& block);
    void note(std::string id, stardata::diag::Span span, Kind kind);

    std::vector<Declaration> declarations_;
    std::vector<Use> uses_;
};

} // namespace starcore
