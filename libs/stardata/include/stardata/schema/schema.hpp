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

    // §7.2's dependent type: the name of a sibling key whose *value* is this
    // key's type. `initial` on a `global` is the case that forced it --
    //
    //     key = { name = type     type = type_expr }
    //     key = { name = initial  type_of = type }
    //
    // -- because §6.4 gives a global "the same types as properties, including
    // collections", so `initial` accepts a scalar for one global and a map
    // for the next, and no fixed `type =` covers both.
    //
    // THE ALTERNATIVE WAS AN ESCAPE HATCH, and it was tried: a type called
    // `any` that accepted anything and meant "checked somewhere else". It
    // worked for globals and left every other schema in the program able to
    // opt out of type checking by writing one word. A dependent type says
    // the same thing about one key without saying it about all of them.
    //
    // Two more callers wait in the specification: §11.1's
    // `set = { target  prop  value }` types `value` by `prop`, and
    // `set_global = { id  value }` types it by the global `id` names.
    std::string type_of;

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

    // §7.2's last row, which is a field of the *schema* and not of a key:
    // "the ordered stages of this form, through which type narrowing flows
    // (§8.8.3). Declaring it keeps the narrowing analysis free of any
    // knowledge of what the stages are."
    //
    // That parenthesis is the whole reason it exists, and it is the one
    // thing that keeps backlog F12 on the right side of proposal §2.1.1: a
    // dataflow that ran over a built-in list `when, conditions,
    // restrictions, effects` would be `libs/stardata` naming four pieces of
    // interactive-fiction vocabulary. Running over whatever sequence the
    // schema declares, it names none of them -- and a ruleset that invents a
    // fifth stage gets narrowing through it for free.
    std::vector<std::string> stage_order;
    diag::Span stage_order_span;

    diag::Span span;

    [[nodiscard]] const KeyDecl* find_key(std::string_view name) const noexcept;

    // The key that carries this form's `unique_in` namespace (§7.2), or null
    // for a form whose instances have no id -- `rule` and `prop_def` are
    // both anonymous, and §7.6's uniqueness rule has nothing to say about
    // either.
    //
    // Two callers, and they must agree: the loader registers an instance
    // under this namespace, and `ref<F>` resolves into it. A form declaring
    // more than one is taken at its first, since §7.2 gives an instance one
    // id and one namespace.
    [[nodiscard]] const KeyDecl* unique_key() const noexcept;
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
    std::string of_class; // empty for a trait, and for the root
    bool is_trait = false;
    bool sealed = false;

    // §8.1.1's root: the class a declaration with no `of_class` descends from.
    // At most one class in a program may declare it.
    //
    // DECLARED RATHER THAN NAMED, which is the whole point. "A single
    // inheritance hierarchy has a root" is generic; *which* class is the root
    // is the object model's, and the object model is `libs/starcore`'s. This
    // flag is how the second fact reaches the first without either library
    // naming the other's -- the same move `stage_order` makes for §8.8.3's
    // pipeline. It replaced an `implicit_parent` parameter threaded through
    // three functions, whose default silently reported every property on a
    // broad slot as absent.
    bool is_root = false;

    std::string owner;
    std::vector<PropDecl> properties;

    // §8.3's `traits = { openable lockable }`, in declaration order because
    // §8.4 resolves them in it.
    //
    // Read late, and the gap it closed is worth recording: until backlog F12
    // this key was parsed by nobody, so a property arriving through a trait
    // resolved to nothing at all. Everything that walked a class for a
    // property -- the instantiation type check of F4, the object-local rule
    // of F11 -- was quietly missing every trait property in the program.
    std::vector<std::string> traits;

    diag::Span span;
    diag::Span of_class_span;

    [[nodiscard]] const PropDecl* find_property(std::string_view name) const noexcept;
};

// A `global` or a `const` (§6.4). One structure for both, because the only
// difference the format layer can see is whether the value may change --
// everything about *reading the declaration* is identical, and §6.4 gives
// them one id namespace precisely so that they cannot collide.
//
// A FORMAT FORM (§7.2.4), which is the part worth stating. The engine owns
// what a global MEANS: it is save state, it appears in deltas and in undo
// snapshots. But nothing about parsing `id`, `type` and `initial` is
// interactive fiction, and the alternative -- a schema declaring the keys
// plus a reader in the other library -- is the arrangement that let `class`
// and `read_class` drift apart.
//
// It also removes a hole. `initial`'s type is the *value of the `type` key
// beside it*, a dependent type no key declaration can express; while `global`
// was declared elsewhere the key had to be typed `any`, which meant any
// schema anywhere could opt out of type checking. Reading the form here means
// there is no key to type, and the check happens against the real type.
struct GlobalDecl {
    std::string id;
    ast::TypeRef type;
    bool is_const = false;
    std::string owner;
    diag::Span span;      // the id's value, which is the name a reader looks for
    diag::Span type_span; // for reporting a type that names nothing

    [[nodiscard]] bool is_bool() const noexcept { return type.name == "bool"; }
};

// A `class_extension` (§8.2): adds properties and defaults to a class or a
// trait declared elsewhere, possibly in a library the author cannot edit.
//
// ONE FORM FOR BOTH, with the target named by `of_class` or by `of_trait` --
// §7.2.1's exclusive group, so writing both or neither is an error the schema
// states rather than a rule the reader knows. The alternative considered was a
// second form, `trait_extension`, mirroring this one key for key; two forms
// that differ in one identifier are two places to fix every later change.
//
// Naming which one is not pedantry. `class` and `trait` are separate
// namespaces (§8.3), so an id can be both, and a single `of =` would extend
// whichever the lookup happened to try first -- silently, and differently
// depending on load order.
struct ExtensionDecl {
    std::string target;
    bool targets_trait = false;
    std::vector<PropDecl> properties;
    diag::Span span;
    diag::Span target_span;
    bool declares_reparent = false; // §8.2 forbids it; F2a reports it
    diag::Span reparent_span;

    // The key that named the target, for a diagnostic that has to quote it.
    [[nodiscard]] std::string_view target_key() const noexcept {
        return targets_trait ? "of_trait" : "of_class";
    }
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

// A `global` or a `const` (§6.4). `is_const` decides which of `initial` and
// `value` names the starting value and whether the value may change; the rest
// is identical, which is why one reader serves both.
[[nodiscard]] std::optional<GlobalDecl> read_global(const ast::Statement& statement, bool is_const,
                                                    std::string_view owner,
                                                    diag::DiagnosticSink& sink);

// The `prop_def` declarations an object instantiation carries (§8.7, backlog
// F11): properties belonging to this object and to nothing else.
//
// The same reader `class` and `trait` use, because §8.7 is explicit that a
// local property "is otherwise an ordinary property: typed, saved, readable
// in conditions, writable in effects" -- so it had better be read by the
// same code, markers and all, rather than by a second reader that agrees
// with the first until it doesn't.
[[nodiscard]] std::vector<PropDecl>
read_local_prop_defs(const ast::Block& block, const Schema* markers, diag::DiagnosticSink& sink);

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
