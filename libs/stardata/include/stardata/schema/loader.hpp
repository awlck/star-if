// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stardata/cst/green.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/schema/schema.hpp"

namespace stardata::schema {

// Everything a load has declared so far (backlog F2, F3).
//
// THE REGISTRY (backlog F3, spec §13.3). Every declaration is keyed by its
// id, and any library may contribute one: the built-in set arrives first
// because it loads first, not because it is stored anywhere special. A
// library's `stat_block` and core's `action` sit in the same table, are found
// the same way, and are validated against by the same code -- which is what
// §7.1's claim about one source of truth amounts to in practice.
//
// Storage stays a vector in declaration order, with a hash index beside it
// rather than instead of it. The order is load order (§13.2), which is what
// a reader and a diagnostic both expect, and no hash map's iteration order is
// anybody's. The index makes a lookup a hash instead of a scan.
//
// SEALING (backlog F2a, spec §7.2.2) lives here rather than in the loader,
// because it is a property of the *set*: whether a declaration is legal
// depends on what has already been declared and by whom. Every rejection
// names the owner of what it collided with -- an author who has just been
// told "no" is owed the name of whoever said it.
class SchemaSet {
public:
    // One top-level declaration, tracked for the uniqueness and replacement
    // rules of §7.6 whatever kind of thing it declares. `space` is the
    // schema's `unique_in` namespace, which is exactly what §7.2 says that
    // field means -- so `class` and `trait` do not collide, and `const`
    // shares a namespace with `global` because §6.4 says it does.
    struct Declaration {
        std::string space;
        std::string id;
        std::string owner;
        bool sealed = false;
        diag::Span span;
    };

    // What became of a declaration offered to the set.
    enum class Outcome {
        Fresh,    // nothing of that id was there; the caller should store it
        Replaced, // it superseded an earlier one; the caller should overwrite
        Rejected, // reported and discarded; the set is unchanged
    };

    // The §7.6 gate every top-level declaration passes through, before any
    // kind-specific handling. `replaces` is the `@replaces(lib)` the
    // declaration carried, if any.
    Outcome offer(Declaration declaration, const std::optional<Replaces>& replaces,
                  diag::DiagnosticSink& sink);

    // Each returns false when the declaration was rejected, having reported
    // why. A rejected declaration leaves the set exactly as it was.
    bool declare(Schema schema, const std::optional<Replaces>& replaces,
                 diag::DiagnosticSink& sink);
    bool declare_class(ClassDecl decl, const std::optional<Replaces>& replaces,
                       diag::DiagnosticSink& sink);

    // §8.2: an extension adds properties and defaults to an existing class.
    // It may not retype what the class already declares, and may not change
    // the class's parent.
    bool apply_extension(const ExtensionDecl& extension, diag::DiagnosticSink& sink);

    // §7.5: an extension adds keys to an existing form, including a sealed
    // one. Redeclaring a key identically is redundant and warns; any
    // difference is a redefinition and errors.
    bool apply_schema_extension(const SchemaExtensionDecl& extension, diag::DiagnosticSink& sink);

    // A library's own manifest (§13.3). `provides_schema` lists the forms
    // the library says it contributes; it declares nothing and creates
    // nothing, and a mismatch against what the library actually declared is
    // a warning. The spec at one point described it as the mechanism for
    // adding keys to an existing form; that is `schema_extension` (§7.5),
    // and this is a manifest.
    struct LibraryManifest {
        std::string id;
        std::string owner;
        std::vector<std::string> provides_schema;
        diag::Span span;
        diag::Span provides_span;
        bool declares_provides = false;
    };

    bool declare_enum(EnumDecl decl, const std::optional<Replaces>& replaces,
                      diag::DiagnosticSink& sink);

    // §6.4's `global` and `const`. One namespace for both, which is why they
    // share `offer`'s `global` space and one lookup.
    bool declare_global(GlobalDecl decl, const std::optional<Replaces>& replaces,
                        diag::DiagnosticSink& sink);

    // A property one object declared for itself (§8.7, backlog F11).
    //
    // Tracked because §8.8.2's classification needs it: a slot typed `thing`
    // reading a property that only `reactor_console` declares is "possibly
    // present", and a walk over classes alone would call it definitely absent
    // and reject correct code. The class is recorded so the descendant test
    // can ask whether such an object could satisfy the slot at all.
    struct LocalProperty {
        std::string name;
        std::string class_id;
        diag::Span span;
    };

    void add_library(LibraryManifest manifest);
    void add_requirement(CoreRequirement requirement);
    void add_local_property(LocalProperty property);

    [[nodiscard]] const Schema* find(std::string_view id) const noexcept;
    [[nodiscard]] const EnumDecl* find_enum(std::string_view id) const noexcept;

    // §6.4's single namespace: a `global` and a `const` cannot share an id, so
    // one lookup answers for both and the caller reads `is_const` if it cares.
    [[nodiscard]] const GlobalDecl* find_global(std::string_view id) const noexcept;
    [[nodiscard]] const Declaration* find_declaration(std::string_view space,
                                                      std::string_view id) const noexcept;

    // Classes and traits are separate namespaces (§7.2.4 gives each its own
    // `unique_in`), so `class thing` and `trait thing` may both exist and a
    // lookup has to say which it wants. Only the first is instantiable:
    // §7.4's rule is that a top-level statement whose key names a *class*
    // creates one, and a trait is mixed in rather than created.
    [[nodiscard]] const ClassDecl* find_class(std::string_view id) const noexcept;
    [[nodiscard]] const ClassDecl* find_trait(std::string_view id) const noexcept;

    // Either, class first. For the callers that genuinely do not care --
    // `class_extension` names a declaration to add properties to, and §8.2
    // draws no distinction there.
    [[nodiscard]] const ClassDecl* find_class_or_trait(std::string_view id) const noexcept;

    // §8.1.1's root: the class a declaration with no `of_class` descends
    // from, or null when nothing claims it.
    //
    // Read out of the data rather than named in code. A caller loading only
    // schemas of its own gets null and a hierarchy that stops where each tree
    // stops, which is the right answer for a set with no object model in it.
    [[nodiscard]] const ClassDecl* root_class() const noexcept;

    [[nodiscard]] const std::vector<Schema>& schemas() const noexcept { return schemas_; }
    [[nodiscard]] const std::vector<ClassDecl>& classes() const noexcept { return classes_; }
    [[nodiscard]] const std::vector<EnumDecl>& enums() const noexcept { return enums_; }
    [[nodiscard]] const std::vector<GlobalDecl>& globals() const noexcept { return globals_; }
    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept {
        return declarations_;
    }
    [[nodiscard]] const std::vector<CoreRequirement>& requirements() const noexcept {
        return requirements_;
    }
    [[nodiscard]] const std::vector<LibraryManifest>& libraries() const noexcept {
        return libraries_;
    }
    [[nodiscard]] const std::vector<LocalProperty>& local_properties() const noexcept {
        return local_properties_;
    }

private:
    // Index into one of the vectors below, or nothing. Returned rather than a
    // pointer wherever the caller goes on to mutate the vector, since a
    // pointer into it is one `push_back` away from dangling.
    [[nodiscard]] std::optional<std::size_t> schema_index(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> class_index(std::string_view id,
                                                         bool is_trait) const noexcept;
    [[nodiscard]] std::optional<std::size_t> declaration_index(std::string_view space,
                                                               std::string_view id) const noexcept;

    std::vector<Schema> schemas_;
    std::vector<ClassDecl> classes_;
    std::vector<EnumDecl> enums_;
    std::vector<GlobalDecl> globals_;
    std::vector<Declaration> declarations_;
    std::vector<CoreRequirement> requirements_;
    std::vector<LibraryManifest> libraries_;
    std::vector<LocalProperty> local_properties_;

    // id -> position in the vector beside it. Classes and traits share one
    // vector and get one map each, because they are two namespaces (§7.2.4)
    // stored together for the reason `ClassDecl` is one structure: every
    // assertion §7.2.2 makes about a core class it makes about a core trait.
    std::unordered_map<std::string, std::size_t> schema_index_;
    std::unordered_map<std::string, std::size_t> class_index_;
    std::unordered_map<std::string, std::size_t> trait_index_;
    std::unordered_map<std::string, std::size_t> enum_index_;
    std::unordered_map<std::string, std::size_t> global_index_;

    // Keyed by namespace and id both, since §7.6's uniqueness rule is stated
    // per `unique_in` namespace: `const` and `global` share one, `class` and
    // `trait` do not.
    std::map<std::pair<std::string, std::string>, std::size_t> declaration_index_;
};

// Validates one record block against a schema: unknown keys (§7.3), required
// keys (§7.2), arity (§5.3), exclusive groups (§7.2.1) and the declared type
// of every value (§6.2).
//
// `set` is the registry the type checker resolves `enum<E>`, `ref<C>` and
// `block<S>` against, and may be null -- which is what the bootstrap needs,
// since the first schema block is validated before anything is registered.
// A null set checks everything except types.
//
// Annotations are checked by `check_annotations` (schema/annotation.hpp)
// rather than here, since §5.4.1 needs no schema to answer -- but §5.4.1 does
// reach this function: the arity check counts two `@platform` alternatives
// with disjoint frontends as one binding, because both ship and the engine
// selects one per session.
void validate_block(const ast::Block& block, const Schema& schema, const SchemaSet* set,
                    diag::DiagnosticSink& sink);

// Who a set of files is loaded on behalf of. The owner is assigned here and
// never read from the file, so that a library cannot claim to be `starcore`
// by writing so.
//
// It is the **library id** -- `stdlib`, `starscape` -- because that is
// the name `@replaces(lib)` uses (§7.6), and a diagnostic that named a path
// would be telling an author something they cannot write. The built-in set's
// owner is `starcore`, which is not a library, and so is a name no
// `@replaces` can successfully claim.
struct LoadOptions {
    std::string owner;

    // Whether these files are `starcore`'s own. §7.2.5.1 reserves
    // `core_requirement` to core, so the loader has to know -- and it is told
    // rather than deciding, because `libs/stardata` does not know that a
    // thing called `starcore` exists and should not learn.
    bool is_core = false;

    // Record source paths relative to this directory, when they are under
    // it. A diagnostic naming `libs/starcore/builtin/schema.star` is one an
    // author can act on; the same diagnostic naming
    // `/home/somebody/checkout/libs/...` is a path that means nothing to
    // anyone but the machine that produced it, and bakes that machine into
    // any golden that captures it.
    //
    // Empty means "record what you were given", which is the right default
    // for a caller loading one absolute path it chose itself.
    std::filesystem::path name_relative_to;
};

// Parses every `*.star` directly inside `directory`, in sorted order, and
// folds its declarations into `set`.
//
// Two passes over the files: schemas first, everything else second, so that
// a form may be declared in one file and used in another regardless of which
// sorts first. Cross-file forward reference is the rule everywhere else in
// the format (§13.2) and it would be strange for schemas alone to depend on
// filename order.
//
// Returns the paths it loaded, so a caller can report an empty directory as
// the mistake it almost certainly is.
std::vector<std::filesystem::path> load_directory(const std::filesystem::path& directory,
                                                  const LoadOptions& options,
                                                  diag::SourceManager& sources,
                                                  cst::GreenCache& cache, SchemaSet& set,
                                                  diag::DiagnosticSink& sink);

// The same, for a list of files already chosen.
void load_files(const std::vector<std::filesystem::path>& files, const LoadOptions& options,
                diag::SourceManager& sources, cst::GreenCache& cache, SchemaSet& set,
                diag::DiagnosticSink& sink);

// The same, for one source already registered with the SourceManager --
// which is how a caller loads something that never came from a file, and how
// the tests load a would-be library one string at a time.
void load_source(diag::SourceId source, const LoadOptions& options,
                 const diag::SourceManager& sources, cst::GreenCache& cache, SchemaSet& set,
                 diag::DiagnosticSink& sink);

// Checks every `core_requirement` the set collected (backlog F2a, spec
// §7.2.2's "the absence of anything core requires -- reported at load,
// naming the requirement").
//
// This is the anti-wart check, and the reason it is worth its weight is in
// §7.2.2: ADRIFT 5 needs the library to create its location properties and
// Inform 7 attaches meaning to the eighth action declared, and neither says
// so. Here core says so, in data, with an id -- so the failure is a named
// diagnostic at load rather than a bewildering one much later.
void check_requirements(const SchemaSet& set, diag::DiagnosticSink& sink);

// Checks each library's `provides_schema` against the forms it actually
// declared (§13.3). A warning either way: a form listed and not declared, or
// declared and not listed. Like `check_requirements`, this runs after
// everything has loaded, since a library may declare its forms in any file.
void check_library_manifests(const SchemaSet& set, diag::DiagnosticSink& sink);

} // namespace stardata::schema
