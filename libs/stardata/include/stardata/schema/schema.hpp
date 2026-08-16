// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

    // Whether two declarations of the same key say the same thing. §7.5
    // turns on this: a `schema_extension` redeclaring a key identically is
    // redundant (a warning), and any difference is a redefinition wearing an
    // extension's clothes (an error). Position and documentation are not
    // part of the comparison -- two libraries agreeing about a key while
    // wording its `doc` differently have not disagreed about anything.
    [[nodiscard]] bool same_as(const KeyDecl& other) const;
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

// What a property tells the engine about itself (§7.2.3, backlog F2b).
//
// The point of markers is that core depends on *declared, checkable* facts
// rather than on names it has memorised. The engine never learns that a
// property called `open` affects what is in scope; it learns that some
// properties do, and is told which. A ruleset whose equivalent is called
// `shuttered` gets the same behaviour by declaring the same marker, which is
// the whole difference between this and the ADRIFT failure of §7.2.2.
//
// A NAME-TO-FLAG MAP, NOT NAMED FIELDS. The vocabulary belongs to the
// `prop_marker` form in `libs/starcore/builtin/schema.star`, and naming the
// markers here would put a second copy of it in C++ -- which is the same
// mistake one level down, and would mean adding a marker took an edit to
// this header, to the reader, and to the schema. It takes an edit to the
// schema and a line in `starcore` that acts on the marker. Nothing here.
class PropMarkers {
public:
    [[nodiscard]] bool is_set(std::string_view name) const noexcept;
    void set(std::string name, bool value);

    // Every marker written on the property, in declaration order.
    [[nodiscard]] const std::vector<std::pair<std::string, bool>>& all() const noexcept {
        return flags_;
    }
    [[nodiscard]] bool empty() const noexcept { return flags_.empty(); }

private:
    std::vector<std::pair<std::string, bool>> flags_;
};

// One entry of a `prop_def` block (§8.1), written either as a bare type or
// as a block carrying markers (§7.2.3).
struct PropDecl {
    std::string name;
    ast::TypeRef type;
    PropMarkers markers;
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

// A `schema_extension` (§7.5): adds keys to an existing form, including a
// sealed one.
//
// The distinction it exists to draw is §7.2.2's: **sealing prevents
// redefinition, not extension.** A ruleset may add `stamina_cost` to the
// core `action` form and may not change what `id` means, and before this
// form existed the spec pointed at `provides_schema` -- a manifest field,
// never a mechanism -- as though it were the way to do the first.
struct SchemaExtensionDecl {
    std::string of_schema;
    std::vector<KeyDecl> keys;
    diag::Span span;
    diag::Span of_schema_span;
};

// `@replaces(lib)` on a top-level declaration (§7.6): this supersedes the
// declaration of the same id contributed by library `lib`.
//
// The argument names a library rather than a file, and it is an error if
// that library declared no such thing. That check is the whole value of
// naming a source: a typo, an upstream rename, or a library that stopped
// shipping the thing being patched all become build failures instead of a
// new declaration that silently never takes effect.
struct Replaces {
    std::string source; // the library id being superseded
    diag::Span span;
};

// The `@replaces` on a statement's value, if it carries one. Empty when it
// does not, which is the ordinary case.
[[nodiscard]] std::optional<Replaces> read_replaces(const ast::Statement& statement);

// An `enum` declaration (§6.2): a closed set of named values, usable as a
// type. Read structurally because more than the type checker needs it --
// §8.5's placement keywords are the values of one, so the set of legal
// relation names is data rather than a list in the code.
struct EnumDecl {
    std::string id;
    std::vector<std::string> values;
    std::string owner;
    diag::Span span;

    [[nodiscard]] bool has_value(std::string_view value) const noexcept;
};

// Something `starcore` requires of the data it is handed, declared as data
// rather than assumed (§7.2.2, and §7.2.5 for the form itself).
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
//
// `markers` is the `prop_marker` schema, used to check the marker block of
// §7.2.3. It is passed rather than looked up because the readers run below
// the registry; a null one skips marker checking, which is what the
// bootstrap needs before the form is declared.
[[nodiscard]] std::optional<ClassDecl> read_class(const ast::Statement& statement,
                                                  std::string_view owner, const Schema* markers,
                                                  diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<ExtensionDecl> read_class_extension(const ast::Statement& statement,
                                                                const Schema* markers,
                                                                diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<SchemaExtensionDecl>
read_schema_extension(const ast::Statement& statement, diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<CoreRequirement> read_core_requirement(const ast::Statement& statement,
                                                                   diag::DiagnosticSink& sink);

[[nodiscard]] std::optional<EnumDecl> read_enum(const ast::Statement& statement,
                                                std::string_view owner, diag::DiagnosticSink& sink);

// Where an object starts out (§8.5, backlog F2c).
//
// Every object has at most one parent, and the parent link carries a
// relation. `in = ornate_box` is sugar for `holder = ornate_box  relation =
// in`, and both spellings produce identical data -- the sugar exists because
// placement is written for nearly every object in a game, the long form
// because it is what the two slots actually are and because a computed
// placement has no keyword to use.
//
// THE SUGAR IS EXPANDED HERE AND NEVER IN THE TREE. §14.2 requires that a
// round-trip reproduce what the author wrote, so `in = box` has to still say
// `in = box` after a parse and a write. This is the semantic view; the CST
// keeps the spelling.
struct Placement {
    std::string holder;   // the id of the containing object
    std::string relation; // one of the values of the relation enum
    bool from_sugar = false;
    diag::Span span; // the key that established it
};

// The placement an object instantiation block declares, if any.
//
// `relations` is the set of legal relation names -- the values of the enum
// that `starcore.object`'s `relation` property is typed by. It is passed in
// rather than known here for the same reason the markers are: `libs/stardata`
// does not know that placement is a thing interactive fiction has, and a list
// of seven words in this file would be a piece of the object model hiding in
// the format library. An empty set means no keyword is sugar, which is what
// a caller with no object model wants.
//
// Reports when a block writes both spellings: they are the same two slots,
// and §8.5 says the conflict is not resolvable by precedence -- so neither
// wins, and nothing is returned.
[[nodiscard]] std::optional<Placement> read_placement(const ast::Block& block,
                                                      const std::vector<std::string>& relations,
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
