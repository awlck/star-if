// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/ast/ast.hpp"
#include "stardata/diag/sink.hpp"

namespace starcore {

// Styles and localisation (spec §9.3 and §9.6, backlog F7).
//
// The vocabulary half of the text layer. `libs/stardata` parses the template
// grammar (stardata/text/template.hpp) and knows no name inside one; this
// file knows the two forms that give those names meaning -- `style`, which
// declares what an author may write, and `loc`, which declares what a `$key`
// resolves to -- and reports the four diagnostics that follow.
//
// WHY THESE TWO FORMS ARE CORE-OWNED. Appendix C lists both under "forms
// supplied by `stdlib` unless noted", which cannot be right for either:
// §7.2.4's test for core ownership is "does `starcore`'s own code read or
// write it?", and this file is that code. A `style` declared by a library
// core cannot name would leave §5.4.1's `@style(id)` annotation with an
// argument nothing can check, and a `loc` table core cannot read would leave
// §9.6's fallback chain -- source language, then a visible `«key»` -- with
// nothing to fall back through. Both are therefore declared in
// libs/starcore/builtin/schema.star and sealed, like every other form core
// reads. §7.2.4's table and Appendix C now say so.
//
// TWO PHASES, because §13.2 lets a declaration and its use sit in different
// files in either order. `add_file` collects; `check` reports once
// everything is in. This is the shape `check_requirements` and
// `check_library_manifests` already have, for the same reason.

// The declarations and references one project's files carry.
class TextIndex {
public:
    struct Style {
        std::string name;
        stardata::diag::Span span;
    };

    struct LocEntry {
        std::string lang;
        std::string key;
        stardata::diag::Span span;
    };

    struct Reference {
        std::string name;
        stardata::diag::Span span;
    };

    // Reads one file: the styles it declares, the localisation entries it
    // declares, and every reference to either.
    //
    // Reports E-LOC-DUPLICATE, which is the one of the four that can be
    // decided from a single file -- because a single file is its scope.
    //
    // §9.6 says localisation keys are "unique within a language" and names no
    // file, and the widest reading of that forbids a game from overriding a
    // library's default message -- which the `_default` suffix on every one
    // of stdlib's presumes is possible. So: two entries in one table are an
    // ambiguity nothing can resolve and are an error citing both spans, while
    // a later file superseding an earlier one is what load order already
    // means everywhere else in the format (§13.2). Spec A41 records it.
    void add_file(const stardata::ast::File& file, stardata::diag::DiagnosticSink& sink);

    // Reports what only the whole picture can decide.
    //
    //   E-STYLE-UNDECLARED  a `@style(id)` naming no declared style (§9.3),
    //                       with a suggestion.
    //   E-LOC-UNDEFINED     a `$key` no `loc` table defines, in any language
    //                       (§9.6), with a suggestion.
    //   W-LOC-UNUSED        a declared key nothing references.
    //
    // "In any language" is §9.6's fallback chain read forwards: a key present
    // in the source language and missing from a translation is what the
    // chain exists to survive, and reporting it here would make adding a
    // language an error rather than a partial translation.
    void check(stardata::diag::DiagnosticSink& sink) const;

    [[nodiscard]] const std::vector<Style>& styles() const noexcept { return styles_; }
    [[nodiscard]] const std::vector<LocEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const std::vector<Reference>& style_references() const noexcept {
        return style_references_;
    }
    [[nodiscard]] const std::vector<Reference>& loc_references() const noexcept {
        return loc_references_;
    }

private:
    void read_style(const stardata::ast::Block& block);
    void read_loc(const stardata::ast::Block& block, std::size_t first_entry,
                  stardata::diag::DiagnosticSink& sink);
    void collect_annotations(const stardata::ast::Value& value);
    void walk(const stardata::ast::Value& value, stardata::diag::DiagnosticSink& quiet);

    std::vector<Style> styles_;
    std::vector<LocEntry> entries_;
    std::vector<Reference> style_references_;
    std::vector<Reference> loc_references_;
};

} // namespace starcore
