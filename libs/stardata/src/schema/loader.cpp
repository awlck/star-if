// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/loader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/annotation.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/schema/types.hpp"

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

// A path as the SourceManager should record it: relative to the caller's
// base when it is under one, and forward-slashed either way, so that a
// diagnostic reads the same on every platform and in every checkout.
[[nodiscard]] std::filesystem::path source_name(const std::filesystem::path& path,
                                                const std::filesystem::path& base) {
    if (base.empty()) {
        return std::filesystem::path(path.generic_string());
    }
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(path, base, ec);
    if (ec || relative.empty() || *relative.begin() == "..") {
        return std::filesystem::path(path.generic_string());
    }
    return std::filesystem::path(relative.generic_string());
}

// The forms this loader understands structurally, as opposed to the forms it
// merely validates against a schema. Each is a declaration *about* the schema
// layer, so the layer has to read it itself.
[[nodiscard]] bool is_structural_form(std::string_view key) noexcept {
    return key == "schema" || key == "class" || key == "trait" || key == "class_extension" ||
           key == "schema_extension" || key == "core_requirement" || key == "enum";
}

} // namespace

// --- SchemaSet ---------------------------------------------------------

// The registry proper (backlog F3). Every `find` below is a hash lookup into
// an index kept beside the declaration-order vector, rather than the scan
// that stood in for it while F2 was being written.
//
// The maps are keyed by `std::string` and probed with a `std::string_view`,
// which C++20 heterogeneous lookup does not give unordered containers without
// a transparent hash, so each probe below constructs one string. That is a
// deliberate trade for now: the alternative is a custom hash and equality
// pair on every map, and the cost is a small allocation on a path that used
// to walk the whole vector.
std::optional<std::size_t> SchemaSet::schema_index(std::string_view id) const noexcept {
    const auto it = schema_index_.find(std::string(id));
    return it == schema_index_.end() ? std::nullopt : std::optional<std::size_t>(it->second);
}

std::optional<std::size_t> SchemaSet::class_index(std::string_view id,
                                                  bool is_trait) const noexcept {
    const std::unordered_map<std::string, std::size_t>& index =
        is_trait ? trait_index_ : class_index_;
    const auto it = index.find(std::string(id));
    return it == index.end() ? std::nullopt : std::optional<std::size_t>(it->second);
}

std::optional<std::size_t> SchemaSet::declaration_index(std::string_view space,
                                                        std::string_view id) const noexcept {
    const auto it = declaration_index_.find(std::pair(std::string(space), std::string(id)));
    return it == declaration_index_.end() ? std::nullopt : std::optional<std::size_t>(it->second);
}

const Schema* SchemaSet::find(std::string_view id) const noexcept {
    const std::optional<std::size_t> index = schema_index(id);
    return index ? &schemas_[*index] : nullptr;
}

const EnumDecl* SchemaSet::find_enum(std::string_view id) const noexcept {
    const auto it = enum_index_.find(std::string(id));
    return it == enum_index_.end() ? nullptr : &enums_[it->second];
}

const ClassDecl* SchemaSet::find_class(std::string_view id) const noexcept {
    const std::optional<std::size_t> index = class_index(id, /*is_trait=*/false);
    return index ? &classes_[*index] : nullptr;
}

const ClassDecl* SchemaSet::find_trait(std::string_view id) const noexcept {
    const std::optional<std::size_t> index = class_index(id, /*is_trait=*/true);
    return index ? &classes_[*index] : nullptr;
}

const ClassDecl* SchemaSet::find_class_or_trait(std::string_view id) const noexcept {
    const ClassDecl* decl = find_class(id);
    return decl != nullptr ? decl : find_trait(id);
}

void SchemaSet::add_requirement(CoreRequirement requirement) {
    requirements_.push_back(std::move(requirement));
}

void SchemaSet::add_library(LibraryManifest manifest) {
    libraries_.push_back(std::move(manifest));
}

void SchemaSet::add_local_property(LocalProperty property) {
    local_properties_.push_back(std::move(property));
}

const SchemaSet::Declaration* SchemaSet::find_declaration(std::string_view space,
                                                          std::string_view id) const noexcept {
    const std::optional<std::size_t> index = declaration_index(space, id);
    return index ? &declarations_[*index] : nullptr;
}

// The §7.6 gate. Everything a file declares at the top level comes through
// here, so the uniqueness rule and `@replaces` are written once rather than
// once per kind of declaration -- which is what stopped the earlier version
// of this file from enforcing either on anything but schemas and classes.
SchemaSet::Outcome SchemaSet::offer(Declaration declaration,
                                    const std::optional<Replaces>& replaces,
                                    diag::DiagnosticSink& sink) {
    const std::optional<std::size_t> at = declaration_index(declaration.space, declaration.id);
    const Declaration* existing = at ? &declarations_[*at] : nullptr;

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
            // §14.3 wants a suggestion on this row, and the useful one is
            // over the ids in the same `unique_in` namespace: replacing
            // `stdlib`'s `wait` and misspelling it is a likelier mistake
            // than inventing an id outright.
            std::vector<std::string_view> siblings;
            for (const Declaration& other : declarations_) {
                if (other.space == declaration.space) {
                    siblings.emplace_back(other.id);
                }
            }
            if (!suggest(diagnostic, declaration.span, declaration.id, siblings)) {
                diagnostic.with_fix_it(replaces->span, "", "remove the `@replaces` annotation");
            }
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

        declarations_[*at] = std::move(declaration);
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

    declaration_index_.emplace(std::pair(declaration.space, declaration.id), declarations_.size());
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
    if (const std::optional<std::size_t> at = schema_index(schema.id)) {
        schemas_[*at] = std::move(schema); // §7.6: replacement is total
        return true;
    }
    schema_index_.emplace(schema.id, schemas_.size());
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
    if (const std::optional<std::size_t> at = class_index(decl.id, decl.is_trait)) {
        classes_[*at] = std::move(decl);
        return true;
    }
    (decl.is_trait ? trait_index_ : class_index_).emplace(decl.id, classes_.size());
    classes_.push_back(std::move(decl));
    return true;
}

bool SchemaSet::declare_enum(EnumDecl decl, const std::optional<Replaces>& replaces,
                             diag::DiagnosticSink& sink) {
    const Outcome outcome = offer(
        Declaration{"enum", decl.id, decl.owner, /*sealed=*/false, decl.span}, replaces, sink);
    if (outcome == Outcome::Rejected) {
        return false;
    }
    if (const auto it = enum_index_.find(decl.id); it != enum_index_.end()) {
        enums_[it->second] = std::move(decl);
        return true;
    }
    enum_index_.emplace(decl.id, enums_.size());
    enums_.push_back(std::move(decl));
    return true;
}

bool SchemaSet::apply_schema_extension(const SchemaExtensionDecl& extension,
                                       diag::DiagnosticSink& sink) {
    const std::optional<std::size_t> at = schema_index(extension.of_schema);
    if (!at) {
        Diagnostic diagnostic(Code::SchemaInvalid, extension.of_schema_span,
                              "I can't extend '" + extension.of_schema +
                                  "', because nothing declares it");
        diagnostic.with_note("a schema_extension names a form declared elsewhere -- in a library, "
                             "or earlier in the load order (spec §7.5, §13.2)");
        sink.report(std::move(diagnostic));
        return false;
    }

    const std::size_t index = *at;
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
    // Either namespace: §8.2 draws no distinction, and a trait's property set
    // is extended the same way a class's is.
    const ClassDecl* existing = find_class_or_trait(extension.of_class);
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

namespace {

// "'a', 'b' or 'c'" -- for a diagnostic that §14.3 requires to name a
// group's members. Written out rather than a bare comma list because the
// sentence it lands in reads as a sentence.
[[nodiscard]] std::string list_of(const std::vector<std::string>& names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            text += i + 1 == names.size() ? " or " : ", ";
        }
        text += "'" + names[i] + "'";
    }
    return text;
}

// §5.3: a second **binding** occurrence of an `arity = one` key is an error,
// citing both spans.
//
// Only keys the schema declares are checked. §5.3 states arity as something
// "declared by the schema", so an undeclared key in an `open = yes` form has
// none to violate -- and the forms that are open are open precisely because
// their key set is a question for a later pass (`class` keys are property
// defaults, F11's) rather than one this function can answer.
void check_arity(const std::vector<ast::Statement>& statements, const Schema& schema,
                 diag::DiagnosticSink& sink) {
    std::vector<std::pair<std::string, const ast::Statement*>> bound;

    for (const ast::Statement& statement : statements) {
        if (!statement.is_binding()) {
            continue; // `+=` and `-=` transform a value, they do not bind one
        }
        const std::optional<std::string> name = statement.key_name();
        if (!name || name->empty()) {
            continue;
        }
        const KeyDecl* declared = schema.find_key(*name);
        if (declared == nullptr || declared->arity != Arity::One) {
            continue; // `arity = many` collects them, in source order
        }

        // §5.4.1's conditional presence, made pairwise: two bindings collide
        // only where both apply. `@platform(glk)` and `@platform(qt)` are one
        // binding with a run-time selector -- both ship in the one `.spak`
        // and the engine picks when the frontend declares itself -- so
        // neither is a duplicate of the other. `@debug` separates nothing,
        // because a development build holds the annotated statement and the
        // plain one both.
        const Presence presence = presence_of(statement);

        const ast::Statement* first = nullptr;
        for (const auto& [seen, at] : bound) {
            if (seen != *name || !presence.can_coexist_with(presence_of(*at))) {
                continue;
            }
            first = at;
            break;
        }
        if (first == nullptr) {
            bound.emplace_back(*name, &statement);
            continue;
        }

        Diagnostic diagnostic(Code::DuplicateKey, statement.report_span(),
                              "'" + *name + "' is set twice in this '" + schema.id + "'");
        diagnostic.with_note("first set here", first->report_span());
        diagnostic.with_note("'" + *name +
                             "' holds one value, so the second line would silently win over "
                             "the first. A key that may repeat says `arity = many` in its "
                             "schema (spec §5.3, §7.2)");
        diagnostic.with_note("`+=` and `-=` are not bindings and never collide: write `" + *name +
                             " += ...` to add to what is already there");
        if (!presence.unconditional() || !presence_of(*first).unconditional()) {
            diagnostic.with_note("these two apply together somewhere. `@platform` tells them "
                                 "apart only when their frontends do not overlap -- and a "
                                 "statement without one runs on every frontend, so it overlaps "
                                 "them all. `@debug` tells nothing apart: a development build has "
                                 "the annotated statement and the plain one both (spec §5.4.1)");
        }
        sink.report(std::move(diagnostic));
    }
}

// §7.2.1: within a block, exactly one key of a given `exclusive_group` may
// appear. Two or more is always an error; zero is an error if any member is
// required.
//
// Presence, not binding: `of_action = take` alongside `of_event += ...` is
// still a block that says both, and the point of a group is that the two
// alternatives are answers to one question.
void check_exclusive_groups(const ast::Block& block, const Schema& schema,
                            diag::DiagnosticSink& sink) {
    // Groups in the order the schema declares them, so two blocks with the
    // same mistake report it the same way.
    std::vector<std::string> groups;
    for (const KeyDecl& key : schema.keys) {
        if (key.exclusive_group.empty()) {
            continue;
        }
        if (std::find(groups.begin(), groups.end(), key.exclusive_group) == groups.end()) {
            groups.push_back(key.exclusive_group);
        }
    }

    for (const std::string& group : groups) {
        std::vector<std::string> members;
        bool any_required = false;
        std::vector<ast::Statement> present;
        for (const KeyDecl& key : schema.keys) {
            if (key.exclusive_group != group) {
                continue;
            }
            members.push_back(key.name);
            any_required = any_required || key.required;
            if (const std::optional<ast::Statement> written = block.find(key.name)) {
                present.push_back(*written);
            }
        }

        // The same reading of §5.4.1 the arity check makes: two members are
        // both answers to the group's one question only where both apply.
        // `@platform(glk)` on one and `@platform(qt)` on the other is one
        // answer per session, which is what the group asks for.
        if (present.size() > 1 &&
            !presence_of(present[0]).can_coexist_with(presence_of(present[1]))) {
            continue;
        }

        if (present.size() > 1) {
            Diagnostic diagnostic(Code::ExclusiveGroup, present[1].report_span(),
                                  "this '" + schema.id + "' sets both '" + *present[0].key_name() +
                                      "' and '" + *present[1].key_name() + "'");
            diagnostic.with_note("'" + *present[0].key_name() + "' is set here",
                                 present[0].report_span());
            diagnostic.with_note("a '" + schema.id + "' takes " + list_of(members) +
                                 " -- they are alternative answers to one question, so having "
                                 "both leaves nothing to decide between them (spec §7.2.1)");
            sink.report(std::move(diagnostic));
            continue;
        }

        if (present.empty() && any_required) {
            Diagnostic diagnostic(Code::ExclusiveMissing, block.span(),
                                  "this '" + schema.id + "' sets none of " + list_of(members));
            diagnostic.with_note("exactly one of them belongs in every '" + schema.id +
                                 "' (spec §7.2.1)");
            sink.report(std::move(diagnostic));
        }
    }
}

} // namespace

void validate_block(const ast::Block& block, const Schema& schema, const SchemaSet* set,
                    diag::DiagnosticSink& sink) {
    const std::vector<ast::Statement> statements = block.statements();

    for (const ast::Statement& statement : statements) {
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
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "'" + *name + "' is not a key that '" + schema.id + "' has");
        diagnostic.with_note("a form is closed unless it says otherwise, so that a mistyped key "
                             "is caught rather than quietly ignored (spec §7.3)");
        // §7.3 asks for the suggestion by name, and this is the case it
        // names: the keys of the schema that just refused one.
        std::vector<std::string_view> declared;
        declared.reserve(schema.keys.size());
        for (const KeyDecl& key : schema.keys) {
            declared.emplace_back(key.name);
        }
        suggest(diagnostic, statement.key() ? statement.key()->span() : statement.report_span(),
                *name, declared);
        sink.report(std::move(diagnostic));
    }

    check_arity(statements, schema, sink);

    // §6.2, backlog F4. Every value whose key the schema declares is checked
    // against the declared type -- for every operator, because §6.3 gives
    // `+=` and `-=` the same collection shape the binding has.
    if (set != nullptr) {
        for (const ast::Statement& statement : statements) {
            const std::optional<std::string> name = statement.key_name();
            if (!name || name->empty()) {
                continue;
            }
            const KeyDecl* declared = schema.find_key(*name);
            const std::optional<ast::Value> value = statement.value();
            if (declared == nullptr || !value) {
                continue;
            }
            check_value("'" + *name + "'", *value, declared->type, *set, sink);
        }
    }

    for (const KeyDecl& key : schema.keys) {
        if (!key.required || block.find(key.name)) {
            continue;
        }
        // A required key in an exclusive group is required *of the group*
        // (§7.2.1: "zero is an error if any member is required"), not of the
        // block. Reporting it here as well would tell an author who correctly
        // wrote `of_action` that they had also forgotten `of_event`.
        if (!key.exclusive_group.empty()) {
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

    check_exclusive_groups(block, schema, sink);
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
    // The marker vocabulary of §7.2.3, if it has been declared yet. Passing
    // it rather than looking it up keeps `read_class` below the registry.
    const Schema* markers = set.find("prop_marker");

    if (key == "class" || key == "trait") {
        if (std::optional<ClassDecl> decl = read_class(statement, options.owner, markers, sink)) {
            set.declare_class(*std::move(decl), replaces, sink);
        }
        return;
    }
    if (key == "class_extension") {
        if (const std::optional<ExtensionDecl> decl =
                read_class_extension(statement, markers, sink)) {
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
    if (key == "enum") {
        if (std::optional<EnumDecl> decl = read_enum(statement, options.owner, sink)) {
            set.declare_enum(*std::move(decl), replaces, sink);
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
            // §7.4: a statement whose key names a *class* instantiates one.
            // A trait is mixed in through `traits = { ... }` and never
            // created, so a top-level statement naming one is not an
            // instantiation and falls through to the unknown-key report.
            //
            // Type-checking the values against the class's declared property
            // types is mechanism and belongs here. What the properties *mean*
            // does not: §8.5's placement sugar used to be expanded on this
            // line and now lives in `libs/starcore`, which runs its own pass
            // over the same trees (proposal §2.1.1). Which keys are permitted
            // at all is backlog F11's.
            const std::optional<ast::Value> value = statement.value();
            if (const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt) {
                check_instantiation(*block, *set.find_class(key), set, sink);
            }
            // The object's own `prop_def` names are recorded by `fold_all`,
            // which has the non-const set.
            return;
        }
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "nothing declares '" + key + "', so I don't know what this is");
        diagnostic.with_note("a top-level statement names either a form declared by a schema, or "
                             "a class, in which case it creates one of them (spec §7.2, §7.4)");
        // Proposal §4.9's worked example: a `class` declaring `outdoors_room`
        // and an instantiation writing `outdoor_room`. Both namespaces are
        // candidates because both are legal here, and a typo does not know
        // which of the two it was aiming at.
        std::vector<std::string_view> candidates;
        for (const Schema& declared : set.schemas()) {
            if (declared.top_level) {
                candidates.emplace_back(declared.id);
            }
        }
        for (const ClassDecl& declared : set.classes()) {
            if (!declared.is_trait) {
                candidates.emplace_back(declared.id);
            }
        }
        suggest(diagnostic, statement.key() ? statement.key()->span() : statement.report_span(),
                key, candidates);
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
        validate_block(*block, *schema, &set, sink);
    }
}

// The property names an object declares for itself (§8.7), kept for the
// §8.8.2 classification that runs once everything has loaded.
//
// Read here rather than inside `check_instantiation` because that takes the
// set by const reference -- it validates, it does not populate. The cost is
// reading the `prop_def` blocks twice, on the small minority of statements
// that are instantiations carrying one.
void note_local_properties(const ast::Statement& statement, const std::string& key,
                           SchemaSet& set) {
    if (set.find_class(key) == nullptr) {
        return; // not an instantiation
    }
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        return;
    }
    // A throwaway sink: whatever these declarations are wrong about,
    // `check_instantiation` has already said so, and saying it twice would
    // double every diagnostic in the corpus.
    diag::DiagnosticSink quiet;
    for (const PropDecl& property : read_local_prop_defs(*block, set.find("prop_marker"), quiet)) {
        set.add_local_property(SchemaSet::LocalProperty{property.name, key, property.span});
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
    // Pass zero: annotations (§3.8, §5.4.1, backlog F5). Registry-free, and
    // run first because it is the pass that decides what a value even claims
    // to do -- an author who wrote `@merge` on a string wants to hear that
    // before they hear what the schema thinks of the string.
    for (const LoadedFile& file : loaded) {
        check_annotations(ast::File::from(cst::SyntaxNode::root(file.green), file.id), sink);
    }

    // Pass one: schemas. A form declared in one file and used in another
    // must work regardless of which sorts first (§13.2).
    for (const LoadedFile& file : loaded) {
        const ast::File view = ast::File::from(cst::SyntaxNode::root(file.green), file.id);
        for (const ast::Statement& statement : view.find_all("schema")) {
            const std::optional<ast::Value> value = statement.value();
            if (const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt) {
                validate_block(*block, schema_of_schemas(), &set, sink);
                for (const ast::Statement& key : block->find_all("key")) {
                    const std::optional<ast::Value> key_value = key.value();
                    if (const std::optional<ast::Block> key_block =
                            key_value ? key_value->as_block() : std::nullopt) {
                        validate_block(*key_block, key_schema(), &set, sink);
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
            note_local_properties(statement, *key, set);

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
        file.id = sources.add_file(source_name(path, options.name_relative_to), read_bytes(path));
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
            // The two are separate namespaces, so ask for the one core wants
            // and only then look in the other -- which is what turns "nothing
            // declares one" into the more useful "it is a class".
            const ClassDecl* decl = want_trait ? set.find_trait(requirement.subject)
                                               : set.find_class(requirement.subject);
            const ClassDecl* other = want_trait ? set.find_class(requirement.subject)
                                                : set.find_trait(requirement.subject);
            if (decl == nullptr && other != nullptr) {
                fail("core needs '" + requirement.subject + "' to be a " + requirement.kind +
                     ", and it is a " + (other->is_trait ? "trait" : "class"));
            } else if (decl == nullptr) {
                fail("core needs a " + requirement.kind + " called '" + requirement.subject +
                     "', and nothing declares one");
            } else if (!decl->sealed) {
                fail("the " + requirement.kind + " '" + requirement.subject +
                     "' has to be sealed, and it is not");
            }
            continue;
        }

        if (requirement.kind == "property") {
            // Either namespace: `starcore.actor` is a trait, and `busy_until`
            // is a property core reads off it exactly as it reads `holder`
            // off the object class.
            const ClassDecl* decl = set.find_class_or_trait(requirement.subject);
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
            const ClassDecl* decl = set.find_class_or_trait(requirement.subject);
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
