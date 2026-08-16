// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/cst/green.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/schema/schema.hpp"

namespace stardata::schema {

// Everything a load has declared so far (backlog F2).
//
// Deliberately a flat vector with a linear lookup. Phase 0 loads dozens of
// declarations, not thousands, and backlog F3 replaces this with the real
// registry -- keyed by form id, contributed to by libraries. Making it fast
// before it is right would be the wrong order.
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

    void add_library(LibraryManifest manifest);
    void add_requirement(CoreRequirement requirement);

    [[nodiscard]] const Schema* find(std::string_view id) const noexcept;
    [[nodiscard]] const ClassDecl* find_class(std::string_view id) const noexcept;
    [[nodiscard]] const Declaration* find_declaration(std::string_view space,
                                                      std::string_view id) const noexcept;

    [[nodiscard]] const std::vector<Schema>& schemas() const noexcept { return schemas_; }
    [[nodiscard]] const std::vector<ClassDecl>& classes() const noexcept { return classes_; }
    [[nodiscard]] const std::vector<Declaration>& declarations() const noexcept {
        return declarations_;
    }
    [[nodiscard]] const std::vector<CoreRequirement>& requirements() const noexcept {
        return requirements_;
    }
    [[nodiscard]] const std::vector<LibraryManifest>& libraries() const noexcept {
        return libraries_;
    }

private:
    std::vector<Schema> schemas_;
    std::vector<ClassDecl> classes_;
    std::vector<Declaration> declarations_;
    std::vector<CoreRequirement> requirements_;
    std::vector<LibraryManifest> libraries_;
};

// Validates one record block against a schema.
//
// F2's share of key validation, and no more: an unknown key in a closed
// schema, and a required key that is absent. Arity and duplicate keys are
// F3, types are F4, combination modes are F5, exclusive groups are F3 --
// each is read into the KeyDecl already, and none is acted on here.
void validate_block(const ast::Block& block, const Schema& schema, diag::DiagnosticSink& sink);

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
