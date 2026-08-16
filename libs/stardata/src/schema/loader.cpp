// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/loader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/diagnostic.hpp"

namespace stardata::schema {

namespace {

using diag::Code;
using diag::Diagnostic;

// Binary, never text mode: spec §2 requires that an author's line endings
// survive, and normalising them on the way in would break the round-trip
// before the parser ever saw the file.
[[nodiscard]] std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// A path as the SourceManager should record it: forward-slashed, so that a
// diagnostic reads the same on every platform.
[[nodiscard]] std::filesystem::path source_name(const std::filesystem::path& path) {
    return std::filesystem::path(path.generic_string());
}

// The forms this loader understands structurally, as opposed to the forms it
// merely validates against a schema. Each is a declaration *about* the schema
// layer, so the layer has to read it itself.
[[nodiscard]] bool is_structural_form(std::string_view key) noexcept {
    return key == "schema" || key == "class" || key == "trait" || key == "class_extension" ||
           key == "schema_extension" || key == "core_requirement";
}

} // namespace

// --- SchemaSet ---------------------------------------------------------

const Schema* SchemaSet::find(std::string_view id) const noexcept {
    for (const Schema& schema : schemas_) {
        if (schema.id == id) {
            return &schema;
        }
    }
    return nullptr;
}

const ClassDecl* SchemaSet::find_class(std::string_view id) const noexcept {
    for (const ClassDecl& decl : classes_) {
        if (decl.id == id) {
            return &decl;
        }
    }
    return nullptr;
}

void SchemaSet::add_requirement(CoreRequirement requirement) {
    requirements_.push_back(std::move(requirement));
}

void SchemaSet::add_library(LibraryManifest manifest) {
    libraries_.push_back(std::move(manifest));
}

const SchemaSet::Declaration* SchemaSet::find_declaration(std::string_view space,
                                                          std::string_view id) const noexcept {
    for (const Declaration& declaration : declarations_) {
        if (declaration.space == space && declaration.id == id) {
            return &declaration;
        }
    }
    return nullptr;
}

// The §7.6 gate. Everything a file declares at the top level comes through
// here, so the uniqueness rule and `@replaces` are written once rather than
// once per kind of declaration -- which is what stopped the earlier version
// of this file from enforcing either on anything but schemas and classes.
SchemaSet::Outcome SchemaSet::offer(Declaration declaration,
                                    const std::optional<Replaces>& replaces,
                                    diag::DiagnosticSink& sink) {
    const Declaration* existing = find_declaration(declaration.space, declaration.id);

    if (replaces) {
        // §7.6: naming the source is the point. A typo, an upstream rename,
        // or a library that stopped shipping the thing being patched all
        // become build failures here rather than a second declaration that
        // silently never takes effect.
        if (existing == nullptr) {
            Diagnostic diagnostic(Code::SchemaInvalid, replaces->span,
                                  "there is no '" + declaration.id + "' to replace");
            diagnostic.with_note("`@replaces` supersedes a declaration that already exists; if "
                                 "this is meant to be a new one, write it without the annotation "
                                 "(spec §7.6)");
            diagnostic.with_fix_it(replaces->span, "", "remove the `@replaces` annotation");
            sink.report(std::move(diagnostic));
            return Outcome::Rejected;
        }
        if (existing->owner != replaces->source) {
            Diagnostic diagnostic(Code::SchemaInvalid, replaces->span,
                                  "'" + declaration.id + "' doesn't come from " + replaces->source);
            diagnostic.with_note("it was declared by " + existing->owner +
                                     ", so that is the name "
                                     "`@replaces` wants (spec §7.6)",
                                 existing->span);
            diagnostic.with_fix_it(replaces->span, "@replaces(" + existing->owner + ")",
                                   "name " + existing->owner + " instead");
            sink.report(std::move(diagnostic));
            return Outcome::Rejected;
        }
        if (existing->sealed) {
            Diagnostic diagnostic(Code::SchemaSealed, replaces->span,
                                  "'" + declaration.id +
                                      "' is sealed, so it can be added to but never replaced");
            diagnostic.with_note("sealed means exactly this: extend freely, never supersede. "
                                 "Replacing it would change the shape of data " +
                                     existing->owner + " reads directly (spec §7.2.2, §7.6)",
                                 existing->span);
            sink.report(std::move(diagnostic));
            return Outcome::Rejected;
        }

        const std::size_t index = static_cast<std::size_t>(existing - declarations_.data());
        declarations_[index] = std::move(declaration);
        return Outcome::Replaced;
    }

    if (existing != nullptr) {
        if (existing->sealed) {
            // The sealed case is the one §7.2.2 exists for, and it gets the
            // message that explains itself: the author has run into a rule
            // they very likely did not know was there.
            Diagnostic diagnostic(Code::SchemaSealed, declaration.span,
                                  "'" + declaration.id +
                                      "' already has a perfectly good definition, and it belongs "
                                      "to " +
                                      existing->owner);
            diagnostic.with_note("a sealed declaration describes data " + existing->owner +
                                     " reads and writes itself, so redefining it would leave the "
                                     "two disagreeing about the same bytes (spec §7.2.2)",
                                 existing->span);
            diagnostic.with_note("you can still add to it: `schema_extension` contributes new keys "
                                 "to an existing form, and `class_extension` new properties to an "
                                 "existing class (spec §7.5, §8.2)");
            sink.report(std::move(diagnostic));
            return Outcome::Rejected;
        }
        Diagnostic diagnostic(Code::SchemaDuplicate, declaration.span,
                              "'" + declaration.id + "' is declared twice");
        diagnostic.with_note("first declared here, by " + existing->owner, existing->span);
        diagnostic.with_note("if the second one is meant to supersede the first, say so: "
                             "`@replaces(" +
                             existing->owner + ")` (spec §7.6)");
        sink.report(std::move(diagnostic));
        return Outcome::Rejected;
    }

    declarations_.push_back(std::move(declaration));
    return Outcome::Fresh;
}

bool SchemaSet::declare(Schema schema, const std::optional<Replaces>& replaces,
                        diag::DiagnosticSink& sink) {
    const Outcome outcome = offer(
        Declaration{"schema", schema.id, schema.owner, schema.sealed, schema.span}, replaces, sink);
    if (outcome == Outcome::Rejected) {
        return false;
    }
    for (Schema& existing : schemas_) {
        if (existing.id == schema.id) {
            existing = std::move(schema); // §7.6: replacement is total
            return true;
        }
    }
    schemas_.push_back(std::move(schema));
    return true;
}

bool SchemaSet::declare_class(ClassDecl decl, const std::optional<Replaces>& replaces,
                              diag::DiagnosticSink& sink) {
    const std::string space = decl.is_trait ? "trait" : "class";
    const Outcome outcome =
        offer(Declaration{space, decl.id, decl.owner, decl.sealed, decl.span}, replaces, sink);
    if (outcome == Outcome::Rejected) {
        return false;
    }
    for (ClassDecl& existing : classes_) {
        if (existing.id == decl.id && existing.is_trait == decl.is_trait) {
            existing = std::move(decl);
            return true;
        }
    }
    classes_.push_back(std::move(decl));
    return true;
}

bool SchemaSet::apply_schema_extension(const SchemaExtensionDecl& extension,
                                       diag::DiagnosticSink& sink) {
    const Schema* existing = find(extension.of_schema);
    if (existing == nullptr) {
        Diagnostic diagnostic(Code::SchemaInvalid, extension.of_schema_span,
                              "I can't extend '" + extension.of_schema +
                                  "', because nothing declares it");
        diagnostic.with_note("a schema_extension names a form declared elsewhere -- in a library, "
                             "or earlier in the load order (spec §7.5, §13.2)");
        sink.report(std::move(diagnostic));
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(existing - schemas_.data());
    bool accepted = true;
    for (const KeyDecl& key : extension.keys) {
        const KeyDecl* declared = schemas_[index].find_key(key.name);
        if (declared == nullptr) {
            // The permitted case, and the one this form exists for: adding
            // to a sealed schema is legal, because sealing prevents
            // redefinition and not extension (§7.2.2).
            schemas_[index].keys.push_back(key);
            continue;
        }
        if (declared->same_as(key)) {
            Diagnostic diagnostic(Code::PropDefRedundant, key.span,
                                  "'" + key.name + "' is already declared on '" +
                                      extension.of_schema + "', in just these words");
            diagnostic.with_note("declared here, by " + schemas_[index].owner, declared->span);
            diagnostic.with_note("harmless, but the line does nothing -- an extension adds keys "
                                 "(spec §7.5)");
            diagnostic.with_fix_it(key.span, "", "remove the redundant key declaration");
            sink.report(std::move(diagnostic));
            continue;
        }
        Diagnostic diagnostic(Code::PropDefTypeMismatch, key.span,
                              "'" + key.name + "' is already a " + declared->type.to_string() +
                                  " on '" + extension.of_schema + "', and this would make it a " +
                                  key.type.to_string());
        diagnostic.with_note("declared here, by " + schemas_[index].owner, declared->span);
        diagnostic.with_note("an extension adds keys; changing one is a redefinition wearing an "
                             "extension's clothes, and on a sealed form it is exactly what "
                             "sealing exists to prevent (spec §7.5, §7.2.2)");
        sink.report(std::move(diagnostic));
        accepted = false;
    }
    return accepted;
}

bool SchemaSet::apply_extension(const ExtensionDecl& extension, diag::DiagnosticSink& sink) {
    const ClassDecl* existing = find_class(extension.of_class);
    if (existing == nullptr) {
        Diagnostic diagnostic(Code::SchemaInvalid, extension.of_class_span,
                              "I can't extend '" + extension.of_class +
                                  "', because nothing declares it");
        diagnostic.with_note("a class_extension names a class declared elsewhere -- in a library, "
                             "or earlier in the load order (spec §8.2, §13.2)");
        sink.report(std::move(diagnostic));
        return false;
    }

    bool accepted = true;

    // §7.2.2 and §8.2: an extension may add, never re-point.
    if (extension.declares_of_class_change) {
        Diagnostic diagnostic(Code::CoreReparent, extension.reparent_span,
                              "a class_extension can't change what '" + extension.of_class +
                                  "' inherits from");
        diagnostic.with_note("extension adds to a class; changing its parent would silently "
                             "rewrite every object of that class, including ones written by "
                             "somebody else (spec §8.2)",
                             existing->span);
        sink.report(std::move(diagnostic));
        accepted = false;
    }

    const std::size_t index = static_cast<std::size_t>(existing - classes_.data());
    for (const PropDecl& property : extension.properties) {
        const PropDecl* declared = classes_[index].find_property(property.name);
        if (declared == nullptr) {
            classes_[index].properties.push_back(property);
            continue;
        }
        if (declared->type.same_as(property.type)) {
            continue; // re-declaring the same type changes nothing
        }
        // §7.2.2's third bullet, and the reason E-PROPDEF-TYPE-MISMATCH
        // already exists: the engine reads this slot at a known type.
        Diagnostic diagnostic(Code::PropDefTypeMismatch, property.span,
                              "'" + property.name + "' is already a " + declared->type.to_string() +
                                  " on " + extension.of_class + ", and this would make it a " +
                                  property.type.to_string());
        diagnostic.with_note("declared here, by " + classes_[index].owner, declared->span);
        if (classes_[index].sealed) {
            diagnostic.with_note("this is a sealed declaration: its properties are read and "
                                 "written by " +
                                 classes_[index].owner +
                                 " at a known type, so "
                                 "retyping one would corrupt data rather than merely surprise "
                                 "somebody (spec §7.2.2)");
        }
        sink.report(std::move(diagnostic));
        accepted = false;
    }
    return accepted;
}

// --- block validation --------------------------------------------------

void validate_block(const ast::Block& block, const Schema& schema, diag::DiagnosticSink& sink) {
    for (const ast::Statement& statement : block.statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (!name || name->empty()) {
            continue;
        }
        if (schema.find_key(*name) != nullptr) {
            continue;
        }
        if (schema.open) {
            continue; // §7.3: permitted and retained
        }
        // The "did you mean ...?" suggestion §7.3 requires is backlog F6,
        // which computes it by edit distance for keys, form names and enum
        // values alike. Reporting the unknown key without one is still worth
        // doing now: a wrong key that loads silently is the failure mode.
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "'" + *name + "' is not a key that '" + schema.id + "' has");
        diagnostic.with_note("a form is closed unless it says otherwise, so that a mistyped key "
                             "is caught rather than quietly ignored (spec §7.3)");
        sink.report(std::move(diagnostic));
    }

    for (const KeyDecl& key : schema.keys) {
        if (!key.required || block.find(key.name)) {
            continue;
        }
        Diagnostic diagnostic(Code::KeyMissing, block.span(),
                              "this '" + schema.id + "' has no '" + key.name + "'");
        if (!key.doc.empty()) {
            diagnostic.with_note(key.doc);
        }
        diagnostic.with_note("'" + key.name + "' is required on every '" + schema.id +
                             "' (spec §7.2)");
        sink.report(std::move(diagnostic));
    }
}

// --- loading -----------------------------------------------------------

namespace {

// One parsed file, kept together so the pass-two walk can revisit it without
// re-parsing. The tree owns its text, so it outlives the file contents.
struct LoadedFile {
    diag::SourceId id;
    cst::GreenNodePtr green;
};

// Pass two: everything that is not a `schema`.
void fold_declaration(const ast::Statement& statement, const std::string& key,
                      const std::optional<Replaces>& replaces, const LoadOptions& options,
                      SchemaSet& set, diag::DiagnosticSink& sink) {
    if (key == "class" || key == "trait") {
        if (std::optional<ClassDecl> decl = read_class(statement, options.owner, sink)) {
            set.declare_class(*std::move(decl), replaces, sink);
        }
        return;
    }
    if (key == "class_extension") {
        if (const std::optional<ExtensionDecl> decl = read_class_extension(statement, sink)) {
            set.apply_extension(*decl, sink);
        }
        return;
    }
    if (key == "schema_extension") {
        if (const std::optional<SchemaExtensionDecl> decl =
                read_schema_extension(statement, sink)) {
            set.apply_schema_extension(*decl, sink);
        }
        return;
    }
    if (key == "core_requirement") {
        // §7.2.5.1: a reserved internal form. Core has a dependency the
        // schema layer cannot otherwise see -- its C++ reads the containment
        // tree directly -- and nothing else does. A library that could assert
        // requirements would be asserting them about other people's data, at
        // load, in core's voice, with no way for the author being refused to
        // tell whose rule they had broken.
        if (!options.is_core) {
            Diagnostic diagnostic(Code::CoreReserved, statement.report_span(),
                                  "'core_requirement' is starcore's alone, and this isn't "
                                  "starcore");
            diagnostic.with_note("core needs it because its C++ reads the world store directly, "
                                 "and the schema layer cannot see C++. A library has no such gap: "
                                 "what a library depends on is checked by being used (spec "
                                 "§7.2.5.1)");
            diagnostic.with_fix_it(statement.report_span(), "",
                                   "remove the core_requirement declaration");
            sink.report(std::move(diagnostic));
            return;
        }
        if (std::optional<CoreRequirement> requirement = read_core_requirement(statement, sink)) {
            set.add_requirement(*std::move(requirement));
        }
        return;
    }
}

// §7.6's uniqueness rule, for every form that is not one of the few the
// loader handles structurally.
//
// The rule is stated in terms of `unique_in` (§7.2), so it reads the
// namespace out of the schema rather than knowing anything about particular
// forms -- which is why `rule` and `loc` are exempt without being named
// here: neither declares a unique id, so several are normal.
void note_generic_declaration(const ast::Statement& statement, const Schema& schema,
                              const std::optional<Replaces>& replaces, const LoadOptions& options,
                              SchemaSet& set, diag::DiagnosticSink& sink) {
    const KeyDecl* unique = nullptr;
    for (const KeyDecl& key : schema.keys) {
        if (!key.unique_in.empty()) {
            unique = &key;
            break;
        }
    }
    if (unique == nullptr) {
        return;
    }

    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    const std::optional<ast::Statement> id = block ? block->find(unique->name) : std::nullopt;
    if (!id) {
        return; // no id to be unique by; validate_block already said so
    }
    const std::optional<ast::Value> id_value = id->value();
    const std::optional<ast::Scalar> scalar = id_value ? id_value->as_scalar() : std::nullopt;
    const std::optional<std::string_view> text = scalar ? scalar->as_identifier() : std::nullopt;
    if (!text || text->empty()) {
        return;
    }

    set.offer(SchemaSet::Declaration{unique->unique_in, std::string(*text), options.owner,
                                     /*sealed=*/false, id->report_span()},
              replaces, sink);
}

// Whether a top-level statement is something the set knows about at all: a
// declared form, or an instantiation of a declared class (§7.4).
void check_top_level(const ast::Statement& statement, const std::string& key, const SchemaSet& set,
                     diag::DiagnosticSink& sink) {
    const Schema* schema = set.find(key);
    if (schema == nullptr) {
        if (set.find_class(key) != nullptr) {
            // §7.4: a statement whose key names a class instantiates one.
            // Checking the keys inside against the class's property set is
            // backlog F3 and F11, once property resolution exists.
            return;
        }
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "nothing declares '" + key + "', so I don't know what this is");
        diagnostic.with_note("a top-level statement names either a form declared by a schema, or "
                             "a class, in which case it creates one of them (spec §7.2, §7.4)");
        sink.report(std::move(diagnostic));
        return;
    }

    if (!schema->top_level) {
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "'" + key +
                                  "' describes the shape of a nested block, not something "
                                  "a file can hold on its own");
        diagnostic.with_note("only a form declared `top_level = yes` may appear at the top level "
                             "of a file (spec §7.2)",
                             schema->span);
        sink.report(std::move(diagnostic));
        return;
    }

    const std::optional<ast::Value> value = statement.value();
    if (const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt) {
        validate_block(*block, *schema, sink);
    }
}

// A `library` declaration's manifest, kept for the §13.3 check that runs
// once everything has loaded.
void collect_library_manifest(const ast::Statement& statement, const std::string& key,
                              const LoadOptions& options, SchemaSet& set) {
    if (key != "library") {
        return;
    }
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        return;
    }

    SchemaSet::LibraryManifest manifest;
    manifest.owner = options.owner;
    manifest.span = statement.report_span();
    if (const std::optional<ast::Statement> id = block->find("id")) {
        manifest.span = id->report_span();
        const std::optional<ast::Value> id_value = id->value();
        const std::optional<ast::Scalar> scalar = id_value ? id_value->as_scalar() : std::nullopt;
        if (const std::optional<std::string_view> text =
                scalar ? scalar->as_identifier() : std::nullopt) {
            manifest.id = std::string(*text);
        }
    }

    if (const std::optional<ast::Statement> provides = block->find("provides_schema")) {
        manifest.declares_provides = true;
        manifest.provides_span = provides->report_span();
        const std::optional<ast::Value> provides_value = provides->value();
        const std::optional<ast::Block> list =
            provides_value ? provides_value->as_block() : std::nullopt;
        if (list) {
            for (const ast::Scalar& entry : list->values()) {
                if (const std::optional<std::string_view> name = entry.as_identifier()) {
                    manifest.provides_schema.emplace_back(*name);
                }
            }
        }
    }
    set.add_library(std::move(manifest));
}

// The two passes, over files already parsed.
void fold_all(const std::vector<LoadedFile>& loaded, const LoadOptions& options, SchemaSet& set,
              diag::DiagnosticSink& sink) {
    // Pass one: schemas. A form declared in one file and used in another
    // must work regardless of which sorts first (§13.2).
    for (const LoadedFile& file : loaded) {
        const ast::File view = ast::File::from(cst::SyntaxNode::root(file.green), file.id);
        for (const ast::Statement& statement : view.find_all("schema")) {
            const std::optional<ast::Value> value = statement.value();
            if (const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt) {
                validate_block(*block, schema_of_schemas(), sink);
                for (const ast::Statement& key : block->find_all("key")) {
                    const std::optional<ast::Value> key_value = key.value();
                    if (const std::optional<ast::Block> key_block =
                            key_value ? key_value->as_block() : std::nullopt) {
                        validate_block(*key_block, key_schema(), sink);
                    }
                }
            }
            if (std::optional<Schema> schema = read_schema(statement, options.owner, sink)) {
                set.declare(*std::move(schema), read_replaces(statement), sink);
            }
        }
    }

    // Pass two: everything else, now that every form is known.
    for (const LoadedFile& file : loaded) {
        const ast::File view = ast::File::from(cst::SyntaxNode::root(file.green), file.id);
        for (const ast::Statement& statement : view.statements()) {
            const std::optional<std::string> key = statement.key_name();
            if (!key || key->empty() || *key == "schema") {
                continue;
            }
            const std::optional<Replaces> replaces = read_replaces(statement);
            check_top_level(statement, *key, set, sink);

            if (is_structural_form(*key)) {
                fold_declaration(statement, *key, replaces, options, set, sink);
                continue;
            }
            // Not one of the few the loader reads itself: an ordinary form,
            // or an instantiation. §7.6's uniqueness rule applies to the
            // first and not the second, since only a form has a schema to
            // read `unique_in` out of.
            if (const Schema* schema = set.find(*key)) {
                note_generic_declaration(statement, *schema, replaces, options, set, sink);
            }
            collect_library_manifest(statement, *key, options, set);
        }
    }
}

} // namespace

void load_files(const std::vector<std::filesystem::path>& files, const LoadOptions& options,
                diag::SourceManager& sources, cst::GreenCache& cache, SchemaSet& set,
                diag::DiagnosticSink& sink) {
    std::vector<LoadedFile> loaded;
    loaded.reserve(files.size());
    for (const std::filesystem::path& path : files) {
        LoadedFile file;
        file.id = sources.add_file(source_name(path), read_bytes(path));
        file.green = cst::parse(sources, file.id, cache, sink);
        loaded.push_back(std::move(file));
    }
    fold_all(loaded, options, set, sink);
}

void load_source(diag::SourceId source, const LoadOptions& options,
                 const diag::SourceManager& sources, cst::GreenCache& cache, SchemaSet& set,
                 diag::DiagnosticSink& sink) {
    std::vector<LoadedFile> loaded;
    loaded.push_back(LoadedFile{source, cst::parse(sources, source, cache, sink)});
    fold_all(loaded, options, set, sink);
}

std::vector<std::filesystem::path> load_directory(const std::filesystem::path& directory,
                                                  const LoadOptions& options,
                                                  diag::SourceManager& sources,
                                                  cst::GreenCache& cache, SchemaSet& set,
                                                  diag::DiagnosticSink& sink) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".star") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    load_files(files, options, sources, cache, set, sink);
    return files;
}

// --- library manifests -------------------------------------------------

void check_library_manifests(const SchemaSet& set, diag::DiagnosticSink& sink) {
    for (const SchemaSet::LibraryManifest& manifest : set.libraries()) {
        if (!manifest.declares_provides) {
            continue; // saying nothing is not a mismatch
        }

        std::vector<std::string> declared;
        for (const Schema& schema : set.schemas()) {
            if (schema.owner == manifest.owner) {
                declared.push_back(schema.id);
            }
        }

        const auto lists = [](const std::vector<std::string>& names, std::string_view id) {
            for (const std::string& name : names) {
                if (name == id) {
                    return true;
                }
            }
            return false;
        };

        for (const std::string& listed : manifest.provides_schema) {
            if (lists(declared, listed)) {
                continue;
            }
            Diagnostic diagnostic(Code::ProvidesMismatch, manifest.provides_span,
                                  manifest.id + " lists '" + listed +
                                      "' in provides_schema, and declares no such form");
            diagnostic.with_note("`provides_schema` is a manifest, so the editor's library browser "
                                 "and a reader can see a library's forms in one place -- it "
                                 "declares nothing itself (spec §13.3)");
            sink.report(std::move(diagnostic));
        }

        for (const std::string& form : declared) {
            if (lists(manifest.provides_schema, form)) {
                continue;
            }
            Diagnostic diagnostic(Code::ProvidesMismatch, manifest.provides_span,
                                  manifest.id + " declares the form '" + form +
                                      "', and does not list it in provides_schema");
            diagnostic.with_note("the manifest is what a reader and the editor's library browser "
                                 "go by, so a form missing from it is a form nobody finds "
                                 "(spec §13.3)");
            sink.report(std::move(diagnostic));
        }
    }
}

// --- core requirements -------------------------------------------------

void check_requirements(const SchemaSet& set, diag::DiagnosticSink& sink) {
    for (const CoreRequirement& requirement : set.requirements()) {
        // Every failure below says the same two things: which requirement,
        // by id, and why it exists. Naming the requirement is the whole
        // point -- an unnamed one is exactly the ADRIFT failure §7.2.2
        // describes, where the engine wants something and never says what.
        const auto fail = [&](std::string message) {
            Diagnostic diagnostic(Code::CoreRequirement, requirement.span, std::move(message));
            diagnostic.with_note("this is core requirement '" + requirement.id +
                                 "'; core checks what it depends on rather than assuming it "
                                 "(spec §7.2.2)");
            if (!requirement.doc.empty()) {
                diagnostic.with_note(requirement.doc);
            }
            sink.report(std::move(diagnostic));
        };

        if (requirement.kind == "form") {
            const Schema* schema = set.find(requirement.subject);
            if (schema == nullptr) {
                fail("core needs a form called '" + requirement.subject +
                     "', and nothing "
                     "declares one");
            } else if (!schema->sealed) {
                fail("the form '" + requirement.subject + "' has to be sealed, and it is not");
            }
            continue;
        }

        if (requirement.kind == "class" || requirement.kind == "trait") {
            const bool want_trait = requirement.kind == "trait";
            const ClassDecl* decl = set.find_class(requirement.subject);
            if (decl == nullptr) {
                fail("core needs a " + requirement.kind + " called '" + requirement.subject +
                     "', and nothing declares one");
            } else if (decl->is_trait != want_trait) {
                fail("core needs '" + requirement.subject + "' to be a " + requirement.kind +
                     ", and it is a " + (decl->is_trait ? "trait" : "class"));
            } else if (!decl->sealed) {
                fail("the " + requirement.kind + " '" + requirement.subject +
                     "' has to be sealed, and it is not");
            }
            continue;
        }

        if (requirement.kind == "property") {
            const ClassDecl* decl = set.find_class(requirement.subject);
            if (decl == nullptr) {
                fail("core needs '" + requirement.subject + "' to declare '" + requirement.member +
                     "', and nothing declares '" + requirement.subject + "' at all");
                continue;
            }
            const PropDecl* property = decl->find_property(requirement.member);
            if (property == nullptr) {
                fail("core needs '" + requirement.subject + "' to declare a property called '" +
                     requirement.member + "', and it does not");
                continue;
            }
            if (requirement.type && !property->type.same_as(*requirement.type)) {
                fail("core reads '" + requirement.subject + "." + requirement.member + "' as a " +
                     requirement.type->to_string() + ", and it is declared as a " +
                     property->type.to_string());
            }
            continue;
        }

        if (requirement.kind == "parent") {
            const ClassDecl* decl = set.find_class(requirement.subject);
            if (decl == nullptr) {
                fail("core needs a class called '" + requirement.subject +
                     "', and nothing declares one");
                continue;
            }
            if (decl->of_class != requirement.member) {
                fail("core needs '" + requirement.subject + "' to derive from '" +
                     requirement.member + "', and it derives from '" +
                     (decl->of_class.empty() ? "nothing" : decl->of_class) + "'");
            }
            continue;
        }

        Diagnostic diagnostic(Code::SchemaInvalid, requirement.span,
                              "'" + requirement.kind +
                                  "' is not a kind of requirement I know how to check");
        diagnostic.with_note("a core_requirement requires a 'form', a 'class', a 'trait', a "
                             "'property' or a 'parent'");
        sink.report(std::move(diagnostic));
    }
}

} // namespace stardata::schema
