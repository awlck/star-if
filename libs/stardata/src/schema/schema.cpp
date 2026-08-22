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

// The identifiers of a list block: `traits = { openable lockable }` and
// `stage_order = { when conditions restrictions effects }` are both this
// shape, and both want source order preserved (§5.1).
[[nodiscard]] std::vector<std::string> identifiers_of(const ast::Block& block,
                                                      std::string_view key) {
    std::vector<std::string> names;
    const std::optional<ast::Value> value = block.value_of(key);
    const std::optional<ast::Block> list = value ? value->as_block() : std::nullopt;
    if (!list) {
        return names;
    }
    for (const ast::Scalar& entry : list->values()) {
        if (const std::optional<std::string_view> name = entry.as_identifier()) {
            names.emplace_back(*name);
        }
    }
    return names;
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

// Which key holds a `global`'s or a `const`'s starting value: §6.4 writes
// `initial` for one and `value` for the other.
[[nodiscard]] std::string_view initial_value_key(bool is_const) noexcept {
    return is_const ? "value" : "initial";
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
    decl.type_of = text_of(*block, "type_of");
    if (!decl.type_of.empty() && !decl.type.name.empty()) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                              "the key '" + decl.name +
                                  "' declares both a 'type' and a 'type_of', and those are two "
                                  "answers to one question");
        diagnostic.with_note("`type_of` names a sibling key whose value is this key's type, for a "
                             "slot whose type is decided by other data -- a `global`'s `initial` "
                             "against its `type` (spec §7.2, §6.4). A key with a fixed type says "
                             "`type`");
        sink.report(std::move(diagnostic));
        decl.type_of.clear();
    }
    if (decl.type.name.empty() && decl.type_of.empty()) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                              "the key '" + decl.name +
                                  "' has no type, so nothing can be "
                                  "checked about what an author writes there");
        diagnostic.with_note("every key declaration carries a type expression, or a `type_of` "
                             "naming the sibling key that supplies one (spec §7.2, §6.2)");
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

// One marker block, checked against the `prop_marker` form.
//
// This is `validate_block` in miniature rather than a call to it, because
// the two live on opposite sides of the loader boundary -- and because the
// message wants to say "marker" rather than "key". §7.2.3's whole argument
// is that the marker vocabulary is a closed, checkable set, so an unknown
// one has to be refused: silently ignoring `affect_scope` would leave an
// author certain they had asked for something they had not.
void validate_marker_block(const ast::Block& block, const Schema& markers,
                           diag::DiagnosticSink& sink) {
    for (const ast::Statement& statement : block.statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (!name || name->empty() || markers.find_key(*name) != nullptr) {
            continue;
        }
        Diagnostic diagnostic(Code::UnknownKey, statement.report_span(),
                              "'" + *name + "' is not a marker I know");
        std::string known;
        for (const KeyDecl& key : markers.keys) {
            if (key.name == "type") {
                continue;
            }
            known += known.empty() ? "" : ", ";
            known += key.name;
        }
        diagnostic.with_note("the markers are " + known +
                             " -- core acts on a declared set, and one it did not recognise "
                             "would be a request nobody answered (spec §7.2.3)");
        sink.report(std::move(diagnostic));
    }
}

// Every marker written on a property, read by name from the block.
//
// Nothing here knows what any marker means -- it reads whatever the
// `prop_marker` form declares as a `bool` and hands the names upward. That
// is the difference between a marker and a magic name, and writing the three
// current markers out as fields here would have quietly reintroduced the
// second: adding a fourth would then take an edit to the schema, to a struct
// and to this function, and the schema would no longer be where the answer
// lived.
//
// With no schema to consult -- only possible before `prop_marker` is
// declared -- every key but `type` is taken as a flag. Unknown ones have
// already been reported by validate_marker_block.
void read_markers(const ast::Block& block, const Schema* markers, PropMarkers& out) {
    for (const ast::Statement& statement : block.statements()) {
        const std::optional<std::string> name = statement.key_name();
        if (!name || name->empty() || *name == "type") {
            continue;
        }
        if (markers != nullptr) {
            const KeyDecl* declared = markers->find_key(*name);
            // A marker the form does not declare, or one whose declared type
            // is not a flag: neither is this function's to guess at. The
            // first is already an error; the second is a marker whose reader
            // has not been written, and inventing a `false` for it would be
            // worse than leaving it absent.
            if (declared == nullptr || declared->type.to_string() != "bool") {
                continue;
            }
        }
        const std::optional<ast::Value> value = statement.value();
        const std::optional<ast::Scalar> scalar = value ? value->as_scalar() : std::nullopt;
        out.set(*name, scalar && scalar->as_bool().value_or(false));
    }
}

// A `prop_def` block: each statement maps a property name either to a bare
// type or to a block carrying markers (§7.2.3).
void read_prop_defs(const ast::Block& owner_block, const Schema* markers,
                    std::vector<PropDecl>& out, diag::DiagnosticSink& sink) {
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
                // The marker form of §7.2.3. Validated against the
                // `prop_marker` form, so an unknown marker is refused by the
                // ordinary closed-schema check rather than by a list here --
                // and adding a marker stays a data change.
                if (markers != nullptr) {
                    validate_marker_block(*marked, *markers, sink);
                }
                if (const std::optional<ast::Value> type = marked->value_of("type")) {
                    if (const std::optional<ast::TypeRef> lowered = type->as_type()) {
                        decl.type = *lowered;
                    }
                }
                read_markers(*marked, markers, decl.markers);
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

bool KeyDecl::same_as(const KeyDecl& other) const {
    return name == other.name && type.same_as(other.type) && required == other.required &&
           arity == other.arity && combine == other.combine && unique_in == other.unique_in &&
           exclusive_group == other.exclusive_group && has_default == other.has_default;
}

const KeyDecl* Schema::find_key(std::string_view name) const noexcept {
    for (const KeyDecl& key : keys) {
        if (key.name == name) {
            return &key;
        }
    }
    return nullptr;
}

const KeyDecl* Schema::unique_key() const noexcept {
    for (const KeyDecl& key : keys) {
        if (!key.unique_in.empty()) {
            return &key;
        }
    }
    return nullptr;
}

bool PropMarkers::is_set(std::string_view name) const noexcept {
    for (const auto& [flag, value] : flags_) {
        if (flag == name) {
            return value;
        }
    }
    return false;
}

void PropMarkers::set(std::string name, bool value) {
    for (auto& [flag, existing] : flags_) {
        if (flag == name) {
            existing = value;
            return;
        }
    }
    flags_.emplace_back(std::move(name), value);
}

bool EnumDecl::has_value(std::string_view value) const noexcept {
    for (const std::string& declared : values) {
        if (declared == value) {
            return true;
        }
    }
    return false;
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

    // §7.2's last row. A field of the schema rather than of a key, which is
    // why it is read here and not in `read_key`.
    schema.stage_order = identifiers_of(*block, "stage_order");
    schema.stage_order_span = schema.span;
    if (const std::optional<ast::Statement> stages = block->find("stage_order")) {
        schema.stage_order_span = stages->report_span();
    }

    for (const ast::Statement& key : block->find_all("key")) {
        if (std::optional<KeyDecl> decl = read_key(key, sink)) {
            schema.keys.push_back(*std::move(decl));
        }
    }
    return schema;
}

std::optional<ClassDecl> read_class(const ast::Statement& statement, std::string_view owner,
                                    const Schema* markers, diag::DiagnosticSink& sink) {
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

    // §8.1.1's root marker. A trait is not in the hierarchy at all (§8.3), so
    // it cannot be its root either; that pairs with the `of_class` rule below
    // and is reported the same way.
    decl.is_root = flag_of(*block, "root");
    if (decl.is_trait && decl.is_root) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.span,
                              "the trait '" + decl.id +
                                  "' is marked `root`, but a trait has no place in the class "
                                  "hierarchy and so cannot be its root");
        diagnostic.with_note("traits are mixed in, not inherited from (spec §8.3); the root is "
                             "the class every other class descends from (spec §8.1.1)");
        sink.report(std::move(diagnostic));
        decl.is_root = false;
    }
    if (decl.is_root && !decl.of_class.empty()) {
        Diagnostic diagnostic(Code::SchemaInvalid, decl.of_class_span,
                              "'" + decl.id + "' is marked `root` and also declares an 'of_class'");
        diagnostic.with_note("the root is where the hierarchy stops -- a class with a parent is "
                             "not it (spec §8.1.1)");
        sink.report(std::move(diagnostic));
        decl.is_root = false;
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

    decl.traits = identifiers_of(*block, "traits");

    read_prop_defs(*block, markers, decl.properties, sink);
    return decl;
}

std::optional<ExtensionDecl> read_class_extension(const ast::Statement& statement,
                                                  const Schema* markers,
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

    // §8.2's target, named by one of two keys. `of_trait` is checked first so
    // that a block naming both -- already an E-EXCLUSIVE-GROUP from the
    // schema's own declaration -- still reads as *something* rather than
    // being dropped: reporting a made-up second error about the class not
    // existing would bury the real one.
    const bool has_class = block->find("of_class").has_value();
    const bool has_trait = block->find("of_trait").has_value();
    decl.targets_trait = has_trait;
    decl.target = text_of(*block, has_trait ? "of_trait" : "of_class");
    decl.target_span = decl.span;
    if (const std::optional<ast::Statement> named = block->find(decl.target_key())) {
        decl.target_span = named->report_span();
    }
    if (decl.target.empty()) {
        if (has_class || has_trait) {
            report_missing(statement, "class_extension", decl.target_key(), sink);
        }
        // Naming neither is the exclusive group's to report, and it has
        // (§7.2.1: zero is an error where any member is required). Saying it
        // again here in different words would be two errors for one mistake.
        return std::nullopt;
    }

    // §8.2: an extension MUST NOT change what it extends. The key naming the
    // target is itself `of_class` or `of_trait`, so the change is spelled
    // with a second one -- which is why this is a count and not a presence
    // check.
    const std::vector<ast::Statement> targets = block->find_all(decl.target_key());
    if (targets.size() > 1) {
        decl.declares_reparent = true;
        decl.reparent_span = targets[1].report_span();
    }

    read_prop_defs(*block, markers, decl.properties, sink);
    return decl;
}

std::optional<SchemaExtensionDecl> read_schema_extension(const ast::Statement& statement,
                                                         diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a schema_extension is a block of key declarations, not a bare "
                              "value");
        diagnostic.with_note("spec §7.5 gives the shape; it mirrors class_extension");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    SchemaExtensionDecl decl;
    decl.span = head_span(statement);
    decl.of_schema = text_of(*block, "of_schema");
    decl.of_schema_span = decl.span;
    if (const std::optional<ast::Statement> target = block->find("of_schema")) {
        decl.of_schema_span = target->report_span();
    }
    if (decl.of_schema.empty()) {
        report_missing(statement, "schema_extension", "of_schema", sink);
        return std::nullopt;
    }

    for (const ast::Statement& key : block->find_all("key")) {
        if (std::optional<KeyDecl> parsed = read_key(key, sink)) {
            decl.keys.push_back(*std::move(parsed));
        }
    }
    return decl;
}

std::optional<Replaces> read_replaces(const ast::Statement& statement) {
    const std::optional<ast::Value> value = statement.value();
    if (!value) {
        return std::nullopt;
    }
    for (const ast::Annotation& annotation : value->annotations()) {
        if (annotation.name() != "replaces") {
            continue;
        }
        Replaces replaces;
        replaces.span = annotation.span();
        const std::vector<cst::SyntaxToken> arguments = annotation.arguments();
        if (!arguments.empty()) {
            replaces.source = std::string(arguments.front().text());
        }
        return replaces;
    }
    return std::nullopt;
}

std::vector<PropDecl> read_local_prop_defs(const ast::Block& block, const Schema* markers,
                                           diag::DiagnosticSink& sink) {
    std::vector<PropDecl> properties;
    read_prop_defs(block, markers, properties, sink);
    return properties;
}

std::optional<EnumDecl> read_enum(const ast::Statement& statement, std::string_view owner,
                                  diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "an enum is a block of declarations, not a bare value");
        diagnostic.with_note("an enum is written `enum = { id = ...  values = { a b c } }` "
                             "(spec §6.2)");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    EnumDecl decl;
    decl.span = head_span(statement);
    decl.owner = std::string(owner);
    decl.id = text_of(*block, "id");
    if (decl.id.empty()) {
        report_missing(statement, "enum", "id", sink);
        return std::nullopt;
    }
    if (const std::optional<ast::Statement> id_statement = block->find("id")) {
        decl.span = id_statement->report_span();
    }

    if (const std::optional<ast::Value> values = block->value_of("values")) {
        if (const std::optional<ast::Block> list = values->as_block()) {
            for (const ast::Scalar& entry : list->values()) {
                if (const std::optional<std::string_view> name = entry.as_identifier()) {
                    decl.values.emplace_back(*name);
                }
            }
        }
    }
    return decl;
}

std::optional<GlobalDecl> read_global(const ast::Statement& statement, bool is_const,
                                      std::string_view owner, diag::DiagnosticSink& sink) {
    const std::string_view form = is_const ? "const" : "global";
    const std::optional<ast::Value> value = statement.value();
    const std::optional<ast::Block> block = value ? value->as_block() : std::nullopt;
    if (!block) {
        Diagnostic diagnostic(Code::SchemaInvalid, head_span(statement),
                              "a " + std::string(form) +
                                  " is a block of declarations, not a bare value");
        diagnostic.with_note("it is written `" + std::string(form) + " = { id = ...  type = ...  " +
                             std::string(initial_value_key(is_const)) + " = ... }` (spec §6.4)");
        sink.report(std::move(diagnostic));
        return std::nullopt;
    }

    GlobalDecl decl;
    decl.is_const = is_const;
    decl.owner = std::string(owner);
    decl.span = head_span(statement);
    decl.id = text_of(*block, "id");
    if (decl.id.empty()) {
        report_missing(statement, form, "id", sink);
        return std::nullopt;
    }
    // The id's VALUE, not the `id` key: the name is what a reader is looking
    // for, and `id` would be the same two characters on every declaration.
    if (const std::optional<ast::Statement> id_statement = block->find("id")) {
        const std::optional<ast::Value> written = id_statement->value();
        decl.span = written ? written->span() : id_statement->report_span();
    }

    const std::optional<ast::Value> declared_type = block->value_of("type");
    const std::optional<ast::TypeRef> type =
        declared_type ? declared_type->as_type() : std::nullopt;
    if (!type) {
        report_missing(statement, form, "type", sink);
        return std::nullopt;
    }
    decl.type = *type;
    decl.type_span = declared_type->span();
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
        result.owner = "stardata";
        result.doc = "The description of a form. Hard-coded because it is what reads the file "
                     "every other schema is written in.";
        result.keys.push_back(bootstrap_key("id", "identifier", /*required=*/true));
        result.keys.push_back(bootstrap_key("doc", "text"));
        result.keys.push_back(bootstrap_key("top_level", "bool"));
        result.keys.push_back(bootstrap_key("open", "bool"));
        result.keys.push_back(bootstrap_key("sealed", "bool"));
        result.keys.push_back(bootstrap_key("stage_order", "list<identifier>"));
        result.keys.push_back(bootstrap_key("key", "block<key>", /*required=*/false, Arity::Many));
        return result;
    }();
    return schema;
}

const Schema& key_schema() {
    static const Schema schema = [] {
        Schema result;
        result.id = "key";
        result.owner = "stardata";
        result.sealed = true;
        result.doc = "One key of a form, per the table in spec §7.2.";
        result.keys.push_back(bootstrap_key("name", "identifier", /*required=*/true));
        // Neither `type` nor `type_of` is required here, though one of them
        // is required of every key. The rule is "exactly one", which is two
        // rules -- not both, not neither -- and `read_key` states both in the
        // author's terms. Marking `type` required as well would report a key
        // that correctly wrote `type_of` for having forgotten `type`.
        result.keys.push_back(bootstrap_key("type", "type_expr"));
        result.keys.push_back(bootstrap_key("type_of", "identifier"));
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
