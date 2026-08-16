// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/schema.hpp"

#include "stardata/diag/diagnostic.hpp"

namespace stardata::schema {

namespace {

using diag::Code;
using diag::Diagnostic;

// A scalar key's value as an identifier, a string or a localisation key,
// whichever it happens to be. Schemas use all three -- `type = int`,
// `doc = "..."`, `doc = $schema_action_doc` -- and a reader that insisted on
// one would reject files the format allows.
[[nodiscard]] std::string text_of(const ast::Block& block, std::string_view key) {
    const std::optional<ast::Value> value = block.value_of(key);
    if (!value) {
        return {};
    }
    const std::optional<ast::Scalar> scalar = value->as_scalar();
    if (!scalar) {
        return {};
    }
    if (const std::optional<std::string_view> identifier = scalar->as_identifier()) {
        return std::string(*identifier);
    }
    if (const std::optional<std::string> string = scalar->as_string()) {
        return *string;
    }
    if (const std::optional<std::string_view> loc = scalar->as_loc_key()) {
        return "$" + std::string(*loc);
    }
    return {};
}

[[nodiscard]] bool flag_of(const ast::Block& block, std::string_view key) {
    const std::optional<ast::Value> value = block.value_of(key);
    if (!value) {
        return false;
    }
    const std::optional<ast::Scalar> scalar = value->as_scalar();
    return scalar && scalar->as_bool().value_or(false);
}

// The span to blame when a declaration is missing something: the key of the
// statement that opened it, never the whole block, so a fifty-line `schema`
// does not underline fifty lines.
[[nodiscard]] diag::Span head_span(const ast::Statement& statement) {
    return statement.report_span();
}

void report_missing(const ast::Statement& statement, std::string_view form, std::string_view key,
                    diag::DiagnosticSink& sink) {
    Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                          "this " + std::string(form) + " has no '" + std::string(key) +
                              "', so I don't know what it is meant to describe");
    diagnostic.with_note("every " + std::string(form) + " declaration needs a '" +
                         std::string(key) + "' (spec §7.2)");
    sink.report(std::move(diagnostic));
}

// One `key = { name = ... type = ... }` inside a schema declaration.
[[nodiscard]] std::optional<KeyDecl> read_key(const ast::Statement& statement,
                                              diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a 'key' in a schema is a block, not a bare value");
        diagnostic.with_note("a key declaration is written `key = { name = ...  type = ... }` "
                             "(spec §7.2)");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    KeyDecl decl;
    decl.span = head_span(statement);
    decl.name = text_of(*block, "name");
    if (decl.name.empty()) {
        report_missing(statement, "key", "name", sink);
        return std::nullopt;
    }
    if (const std::optional<ast::Statement> name_statement = block->find("name")) {
        decl.span = name_statement->report_span();
    }

    if (const std::optional<ast::Value> type = block->value_of("type")) {
        if (const std::optional<ast::TypeRef> lowered = type->as_type()) {
            decl.type = *lowered;
        }
    }
    if (decl.type.name.empty()) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                              "the key '" + decl.name +
                                  "' has no type, so nothing can be "
                                  "checked about what an author writes there");
        diagnostic.with_note("every key declaration carries a type expression (spec §7.2, §6.2)");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    decl.required = flag_of(*block, "required");
    decl.unique_in = text_of(*block, "unique_in");
    decl.exclusive_group = text_of(*block, "exclusive_group");
    decl.editor = text_of(*block, "editor");
    decl.doc = text_of(*block, "doc");
    decl.deprecated = text_of(*block, "deprecated");
    decl.has_default = block->find("default").has_value();

    // `arity` and `combine` are read and kept here, and acted on in F3 and
    // F5 respectively. Reading them now means a schema that declares one is
    // not rejected for it, and that the value is checked at the point the
    // author wrote it rather than at the point it first matters.
    const std::string arity = text_of(*block, "arity");
    if (!arity.empty()) {
        if (const std::optional<Arity> parsed = arity_from_string(arity)) {
            decl.arity = *parsed;
        } else {
            Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                                  "'" + arity + "' is not an arity I know");
            diagnostic.with_note("arity is 'one' or 'many' (spec §7.2, §5.3)");
            diagnostic.with_fix_it(decl.span, "", "write `arity = one` or `arity = many`");
            sink.report(std::move(diagnostic));
        }
    }

    const std::string combine = text_of(*block, "combine");
    if (!combine.empty()) {
        if (const std::optional<Combine> parsed = combine_from_string(combine)) {
            decl.combine = *parsed;
        } else {
            Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                                  "'" + combine + "' is not a combination mode I know");
            diagnostic.with_note(
                "a combination mode is 'override', 'merge', 'append' or 'smart' (spec §5.4.2)");
            sink.report(std::move(diagnostic));
        }
    }

    return decl;
}

// A `prop_def` block: each statement maps a property name either to a bare
// type or to a block carrying markers (§7.2.3).
void read_prop_defs(const ast::Block& owner_block, std::vector<PropDecl>& out,
                    diag::DiagnosticSink& sink) {
    for (const ast::Statement& prop_def : owner_block.find_all("prop_def")) {
        const std::optional<ast::Value> value = prop_def.value();
        const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
        if (!block) {
            Diagnostic diagnostic(Code::SchemaInvalid, prop_def.report_span(),
                                  "'prop_def' is a block of property declarations, not a bare "
                                  "value");
            diagnostic.with_note("a property declaration is written `prop_def = { open = bool }` "
                                 "(spec §8.1)");
            sink.report(std::move(diagnostic));
            continue;
        }

        for (const ast::Statement& statement : block->statements()) {
            const std::optional<std::string> name = statement.key_name();
            if (!name || name->empty()) {
                continue;
            }
            PropDecl decl;
            decl.name = *name;
            decl.span = statement.report_span();

            const std::optional<ast::Value> prop_value = statement.value();
            if (!prop_value) {
                continue;
            }
            if (const std::optional<ast::TypeRef> bare = prop_value->as_type()) {
                decl.type = *bare;
            } else if (const std::optional<ast::Block> marked = prop_value->as_block()) {
                // The marker form of §7.2.3. The markers themselves are F2b;
                // the type is what F2a's assertions turn on, so it is read now.
                if (const std::optional<ast::Value> type = marked->value_of("type")) {
                    if (const std::optional<ast::TypeRef> lowered = type->as_type()) {
                        decl.type = *lowered;
                    }
                }
            }

            if (decl.type.name.empty()) {
                Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                                      "I can't tell what type the property '" + decl.name +
                                          "' is meant to have");
                diagnostic.with_note("a property is declared as `name = type`, or as "
                                     "`name = { type = ... }` when it carries markers "
                                     "(spec §8.1, §7.2.3)");
                sink.report(std::move(diagnostic));
                continue;
            }
            out.push_back(std::move(decl));
        }
    }
}

} // namespace

// --- small conversions -------------------------------------------------

std::string_view to_string(Arity arity) noexcept {
    return arity == Arity::Many ? "many" : "one";
}

std::string_view to_string(Combine combine) noexcept {
    switch (combine) {
    case Combine::Override:
        return "override";
    case Combine::Merge:
        return "merge";
    case Combine::Append:
        return "append";
    case Combine::Smart:
        return "smart";
    }
    return "override";
}

std::optional<Arity> arity_from_string(std::string_view text) noexcept {
    if (text == "one") {
        return Arity::One;
    }
    if (text == "many") {
        return Arity::Many;
    }
    return std::nullopt;
}

std::optional<Combine> combine_from_string(std::string_view text) noexcept {
    if (text == "override") {
        return Combine::Override;
    }
    if (text == "merge") {
        return Combine::Merge;
    }
    if (text == "append") {
        return Combine::Append;
    }
    if (text == "smart") {
        return Combine::Smart;
    }
    return std::nullopt;
}

const KeyDecl* Schema::find_key(std::string_view name) const noexcept {
    for (const KeyDecl& key : keys) {
        if (key.name == name) {
            return &key;
        }
    }
    return nullptr;
}

const PropDecl* ClassDecl::find_property(std::string_view name) const noexcept {
    for (const PropDecl& property : properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

// --- readers -----------------------------------------------------------

std::optional<Schema> read_schema(const ast::Statement& statement, std::string_view owner,
                                  diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a schema is a block of declarations, not a bare value");
        diagnostic.with_note("a schema is written `schema = { id = ...  key = { ... } }` "
                             "(spec §7.2)");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    Schema schema;
    schema.span = head_span(statement);
    schema.owner = std::string(owner);
    schema.id = text_of(*block, "id");
    if (schema.id.empty()) {
        report_missing(statement, "schema", "id", sink);
        return std::nullopt;
    }
    if (const std::optional<ast::Statement> id_statement = block->find("id")) {
        schema.span = id_statement->report_span();
    }

    schema.top_level = flag_of(*block, "top_level");
    schema.open = flag_of(*block, "open");
    schema.sealed = flag_of(*block, "sealed");
    schema.doc = text_of(*block, "doc");

    for (const ast::Statement& key : block->find_all("key")) {
        if (std::optional<KeyDecl> decl = read_key(key, sink)) {
            schema.keys.push_back(*std::move(decl));
        }
    }
    return schema;
}

std::optional<ClassDecl> read_class(const ast::Statement& statement, std::string_view owner,
                                    diag::DiagnosticSink& sink) {
    const std::optional<std::string> form = statement.key_name();
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!form || !block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a class or trait is a block of declarations, not a bare value");
        diagnostic.with_note("spec §8.1 and §8.3 give the shape of each");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    ClassDecl decl;
    decl.span = head_span(statement);
    decl.owner = std::string(owner);
    decl.is_trait = *form == "trait";
    decl.id = text_of(*block, "id");
    if (decl.id.empty()) {
        report_missing(statement, *form, "id", sink);
        return std::nullopt;
    }
    if (const std::optional<ast::Statement> id_statement = block->find("id")) {
        decl.span = id_statement->report_span();
    }

    decl.sealed = flag_of(*block, "sealed");
    decl.of_class = text_of(*block, "of_class");
    decl.of_class_span = decl.span;
    if (const std::optional<ast::Statement> parent = block->find("of_class")) {
        decl.of_class_span = parent->report_span();
    }

    // §8.3: a trait MUST NOT declare `of_class` and MUST NOT participate in
    // the class hierarchy. Reported here rather than left to the class graph,
    // because a trait with a parent is a misunderstanding worth catching at
    // the point it was written.
    if (decl.is_trait && !decl.of_class.empty()) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.of_class_span,
                              "the trait '" + decl.id +
                                  "' has an 'of_class', but a trait has no "
                                  "place in the class hierarchy");
        diagnostic.with_note("traits are mixed in, not inherited from -- that is what lets them "
                             "cut across the class tree (spec §8.3)");
        diagnostic.with_fix_it(decl.of_class_span, "", "remove the 'of_class' line");
        sink.report(std::move(diagnostic));
        decl.of_class.clear();
    }

    read_prop_defs(*block, decl.properties, sink);
    return decl;
}

std::optional<ExtensionDecl> read_class_extension(const ast::Statement& statement,
                                                  diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a class_extension is a block of declarations, not a bare value");
        diagnostic.with_note("spec §8.2 gives the shape");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    ExtensionDecl decl;
    decl.span = head_span(statement);
    decl.of_class = text_of(*block, "of_class");
    decl.of_class_span = decl.span;
    if (const std::optional<ast::Statement> target = block->find("of_class")) {
        decl.of_class_span = target->report_span();
    }
    if (decl.of_class.empty()) {
        report_missing(statement, "class_extension", "of_class", sink);
        return std::nullopt;
    }

    // §8.2: an extension MUST NOT change `of_class`. The key naming the class
    // being extended is itself `of_class`, so the change is spelled with a
    // second one -- which is why this is a count and not a presence check.
    const std::vector<ast::Statement> targets = block->find_all("of_class");
    if (targets.size() > 1) {
        decl.declares_of_class_change = true;
        decl.reparent_span = targets[1].report_span();
    }

    read_prop_defs(*block, decl.properties, sink);
    return decl;
}

std::optional<CoreRequirement> read_core_requirement(const ast::Statement& statement,
                                                     diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a core_requirement is a block, not a bare value");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    CoreRequirement requirement;
    requirement.span = head_span(statement);
    requirement.id = text_of(*block, "id");
    if (requirement.id.empty()) {
        report_missing(statement, "core_requirement", "id", sink);
        return std::nullopt;
    }
    if (const std::optional<ast::Statement> id_statement = block->find("id")) {
        requirement.span = id_statement->report_span();
    }

    requirement.kind = text_of(*block, "requires");
    requirement.subject = text_of(*block, "subject");
    requirement.member = text_of(*block, "member");
    requirement.doc = text_of(*block, "doc");
    if (const std::optional<ast::Value> type = block->value_of("type")) {
        requirement.type = type->as_type();
    }
    return requirement;
}

// --- the bootstrap -----------------------------------------------------

namespace {

[[nodiscard]] KeyDecl bootstrap_key(std::string name, std::string type, bool required = false,
                                    Arity arity = Arity::One) {
    KeyDecl decl;
    decl.name = std::move(name);
    decl.type.name = std::move(type);
    decl.required = required;
    decl.arity = arity;
    return decl;
}

} // namespace

const Schema& schema_of_schemas() {
    static const Schema schema = [] {
        Schema result;
        result.id = "schema";
        result.top_level = true;
        result.sealed = true;
        result.owner = "starcore";
        result.doc = "The description of a form. Hard-coded because it is what reads the file "
                     "every other schema is written in.";
        result.keys.push_back(bootstrap_key("id", "identifier", /*required=*/true));
        result.keys.push_back(bootstrap_key("doc", "text"));
        result.keys.push_back(bootstrap_key("top_level", "bool"));
        result.keys.push_back(bootstrap_key("open", "bool"));
        result.keys.push_back(bootstrap_key("sealed", "bool"));
        result.keys.push_back(bootstrap_key("key", "block<key>", /*required=*/false, Arity::Many));
        return result;
    }();
    return schema;
}

const Schema& key_schema() {
    static const Schema schema = [] {
        Schema result;
        result.id = "key";
        result.owner = "starcore";
        result.sealed = true;
        result.doc = "One key of a form, per the table in spec §7.2.";
        result.keys.push_back(bootstrap_key("name", "identifier", /*required=*/true));
        result.keys.push_back(bootstrap_key("type", "type_expr", /*required=*/true));
        result.keys.push_back(bootstrap_key("required", "bool"));
        result.keys.push_back(bootstrap_key("arity", "arity_enum"));
        result.keys.push_back(bootstrap_key("default", "scalar"));
        result.keys.push_back(bootstrap_key("combine", "combine_enum"));
        result.keys.push_back(bootstrap_key("unique_in", "identifier"));
        result.keys.push_back(bootstrap_key("doc", "text"));
        result.keys.push_back(bootstrap_key("editor", "identifier"));
        result.keys.push_back(bootstrap_key("deprecated", "text"));
        result.keys.push_back(bootstrap_key("exclusive_group", "identifier"));
        return result;
    }();
    return schema;
}

} // namespace stardata::schema
