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
           key == "core_requirement";
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

bool SchemaSet::declare(Schema schema, diag::DiagnosticSink& sink) {
    if (const Schema* existing = find(schema.id)) {
        // The sealed case is the one §7.2.2 exists for, and it gets the
        // message that explains itself: the author has run into a rule they
        // very likely did not know was there.
        if (existing->sealed) {
            Diagnostic diagnostic(Code::SchemaSealed, schema.span,
                                  "'" + schema.id +
                                      "' already has a perfectly good definition, "
                                      "and it belongs to " +
                                      existing->owner);
            diagnostic.with_note("a sealed form describes data " + existing->owner +
                                     " reads and writes itself, so redefining it would leave the "
                                     "two disagreeing about the same bytes (spec §7.2.2)",
                                 existing->span);
            diagnostic.with_note("you can still add to it: `provides_schema` contributes new keys "
                                 "to an existing form (spec §13.3)");
            sink.report(std::move(diagnostic));
            return false;
        }
        Diagnostic diagnostic(Code::SchemaDuplicate, schema.span,
                              "the form '" + schema.id + "' is declared twice");
        diagnostic.with_note("first declared here, by " + existing->owner, existing->span);
        sink.report(std::move(diagnostic));
        return false;
    }
    schemas_.push_back(std::move(schema));
    return true;
}

bool SchemaSet::declare_class(ClassDecl decl, diag::DiagnosticSink& sink) {
    if (const ClassDecl* existing = find_class(decl.id)) {
        const std::string_view noun = existing->is_trait ? "trait" : "class";
        if (existing->sealed) {
            Diagnostic diagnostic(Code::SchemaSealed, decl.span,
                                  "the " + std::string(noun) + " '" + decl.id +
                                      "' already has a perfectly good definition, and it belongs "
                                      "to " +
                                      existing->owner);
            diagnostic.with_note("its properties are the fields of the world store under "
                                 "author-visible names, so they are not a convention to be "
                                 "replaced (spec §7.2.2, §8.1.1)",
                                 existing->span);
            diagnostic.with_note("you can still add to it: `class_extension` adds properties and "
                                 "changes defaults on a class declared elsewhere (spec §8.2)");
            sink.report(std::move(diagnostic));
            return false;
        }
        Diagnostic diagnostic(Code::SchemaDuplicate, decl.span,
                              "the " + std::string(noun) + " '" + decl.id + "' is declared twice");
        diagnostic.with_note("first declared here, by " + existing->owner, existing->span);
        sink.report(std::move(diagnostic));
        return false;
    }
    classes_.push_back(std::move(decl));
    return true;
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
                      const LoadOptions& options, SchemaSet& set, diag::DiagnosticSink& sink) {
    if (key == "class" || key == "trait") {
        if (std::optional<ClassDecl> decl = read_class(statement, options.owner, sink)) {
            set.declare_class(*std::move(decl), sink);
        }
        return;
    }
    if (key == "class_extension") {
        if (const std::optional<ExtensionDecl> decl = read_class_extension(statement, sink)) {
            set.apply_extension(*decl, sink);
        }
        return;
    }
    if (key == "core_requirement") {
        if (std::optional<CoreRequirement> requirement = read_core_requirement(statement, sink)) {
            set.add_requirement(*std::move(requirement));
        }
        return;
    }
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
                set.declare(*std::move(schema), sink);
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
            check_top_level(statement, *key, set, sink);
            if (is_structural_form(*key)) {
                fold_declaration(statement, *key, options, set, sink);
            }
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
