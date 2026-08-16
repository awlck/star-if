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

// The schema layer's model of a declaration (backlog F2, spec §7.2).
//
// Everything here is read out of Stardata -- including, one bootstrap step
// aside, the description of what a `schema` declaration may contain. That is
// the point of §7.1: validation, editor generation and documentation all come
// from one source, and a hard-coded validator would give the first without
// the other two.
//
// The single exception is `schema_of_schemas()` below, which has to be
// hard-coded because it is what reads the files everything else comes from.
// It is kept as small as the bootstrap allows, for the obvious reason that
// anything in it is a rule nobody can see in the source.

enum class Arity : std::uint8_t { One, Many };
enum class Combine : std::uint8_t { Override, Merge, Append, Smart };

[[nodiscard]] std::string_view to_string(Arity arity) noexcept;
[[nodiscard]] std::string_view to_string(Combine combine) noexcept;
[[nodiscard]] std::optional<Arity> arity_from_string(std::string_view text) noexcept;
[[nodiscard]] std::optional<Combine> combine_from_string(std::string_view text) noexcept;

// One `key = { ... }` inside a schema (the table in spec §7.2).
struct KeyDecl {
    std::string name;
    ast::TypeRef type;
    bool required = false;
    Arity arity = Arity::One;
    Combine combine = Combine::Override;
    std::string unique_in;
    std::string exclusive_group;
    std::string editor;
    std::string doc;
    std::string deprecated;
    bool has_default = false;
    diag::Span span; // the key's own name, for a diagnostic to point at
};

// A form: `action`, `class`, `sector`, and so on.
struct Schema {
    std::string id;
    bool top_level = false;
    bool open = false;   // §7.3: an unknown key is permitted and retained
    bool sealed = false; // §7.2.2: redefinition is an error
    std::string owner;   // who declared it; assigned by the loader, never by the file
    std::string doc;
    std::vector<KeyDecl> keys;
    diag::Span span;

    [[nodiscard]] const KeyDecl* find_key(std::string_view name) const noexcept;
};

// One entry of a `prop_def` block (§8.1). The marker form of §7.2.3 --
// `open = { type = bool  affects_scope = yes }` -- parses here as the type
// plus, for now, nothing else: the markers themselves are backlog F2b.
struct PropDecl {
    std::string name;
    ast::TypeRef type;
    diag::Span span;
};

// A class (§8.1) or a trait (§8.3). One structure for both because every
// assertion §7.2.2 makes about a core class it makes identically about a
// core trait, and splitting them would mean writing each check twice.
struct ClassDecl {
    std::string id;
    std::string of_class; // empty for a trait, and for `starcore.object`
    bool is_trait = false;
    bool sealed = false;
    std::string owner;
    std::vector<PropDecl> properties;
    diag::Span span;
    diag::Span of_class_span;

    [[nodiscard]] const PropDecl* find_property(std::string_view name) const noexcept;
};

// A `class_extension` (§8.2): adds properties and defaults to a class
// declared elsewhere, possibly in a library the author cannot edit.
struct ExtensionDecl {
    std::string of_class;
    std::vector<PropDecl> properties;
    diag::Span span;
    diag::Span of_class_span;
    bool declares_of_class_change = false; // §8.2 forbids it; F2a reports it
    diag::Span reparent_span;
};

// Something `starcore` requires of the data it is handed, declared as data
// rather than assumed (§7.2.2).
//
// The list lives in `libs/starcore/builtin/` as ordinary Stardata, so that
// every requirement has an id a diagnostic can name and nobody has to read
// C++ to find out what core is depending on. That is the whole content of
// §7.2.2's rule: if core requires something, it says so out loud.
struct CoreRequirement {
    std::string id;
    std::string kind; // form | class | trait | property | parent
    std::string subject;
    std::string member;
    std::optional<ast::TypeRef> type;
    std::string doc;
    diag::Span span;
};

// --- reading declarations out of a parsed file -------------------------
//
// Each returns nothing when the declaration is too broken to be usable, and
// reports why. A declaration that is merely incomplete still comes back --
// the schema layer is more useful knowing about a half-written form than
// pretending it does not exist, and an editor asks about half-written forms
// constantly.

[[nodiscard]] std::optional<Schema> read_schema(const ast::Statement& statement,
                                                std::string_view owner, diag::DiagnosticSink& sink);

// `class` and `trait` both, distinguished by the statement's key.
[[nodiscard]] std::optional<ClassDecl>
read_class(const ast::Statement& statement, std::string_view owner, diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<ExtensionDecl> read_class_extension(const ast::Statement& statement,
                                                                diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<CoreRequirement> read_core_requirement(const ast::Statement& statement,
                                                                   diag::DiagnosticSink& sink);

// --- the bootstrap -----------------------------------------------------

// The schema for `schema` itself: the one rule in the system not written in
// Stardata, because it is what reads the file the rest are written in.
//
// Deliberately minimal (backlog F2): it describes exactly the fields the
// builtin files use, not the whole of §7.2's table. Anything beyond the
// bootstrap belongs in `libs/starcore/builtin/schema.star`, where it is
// visible, diffable and extensible like everything else.
[[nodiscard]] const Schema& schema_of_schemas();

// The nested `key = { ... }` block inside a schema declaration.
[[nodiscard]] const Schema& key_schema();

} // namespace stardata::schema
