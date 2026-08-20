// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starcore/text.hpp"

#include <optional>
#include <set>
#include <string>
#include <utility>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/suggest.hpp"
#include "stardata/text/template.hpp"

namespace starcore {
namespace {

using stardata::ast::Annotation;
using stardata::ast::Block;
using stardata::ast::Scalar;
using stardata::ast::Statement;
using stardata::ast::Value;
using stardata::diag::Code;
using stardata::diag::Diagnostic;
using stardata::diag::DiagnosticSink;
using stardata::diag::Span;

// §9.3's and §9.6's forms, and the two keys this pass reads inside them.
// Core vocabulary, named here for the reason proposal §2.1.1 gives: this is
// the library that owns it.
constexpr std::string_view kStyleForm = "style";
constexpr std::string_view kLocForm = "loc";
constexpr std::string_view kIdKey = "id";
constexpr std::string_view kLangKey = "lang";

// §5.4.1's `@style(id)`. The annotation is Stardata's -- `libs/stardata`
// validates that it takes exactly one identifier -- and what that identifier
// has to name is §9.3's, which is here.
constexpr std::string_view kStyleAnnotation = "style";

[[nodiscard]] Span span_of(const stardata::cst::SyntaxToken& token,
                           stardata::diag::SourceId source) {
    return Span{source, token.text_range().offset, token.text_range().length};
}

[[nodiscard]] std::vector<std::string_view> names_of(const std::vector<TextIndex::Style>& styles) {
    std::vector<std::string_view> names;
    names.reserve(styles.size());
    for (const TextIndex::Style& style : styles) {
        names.emplace_back(style.name);
    }
    return names;
}

} // namespace

void TextIndex::add_file(const stardata::ast::File& file, DiagnosticSink& sink) {
    // §9.6's uniqueness is checked within this file, and the range is
    // remembered so that a later file cannot collide with it. The header
    // says why that scope and not a wider one.
    const std::size_t first_entry = entries_.size();

    // A quiet sink for the general walk over strings, and only for it.
    //
    // The walk cannot tell a template from a parser grammar line -- `match =
    // { "take [something]" }` is a string with brackets in it and is not a
    // template (§15 reserves `[` and `]` for both) -- because that is a fact
    // about the value's declared type, which the schema layer holds and this
    // pass does not walk. So the walk collects `@style(...)` directives,
    // which no grammar line has, and says nothing about brackets.
    // E-TEMPLATE-BRACKETS is reported where the type is known: by the type
    // checker for a declared `text` value, and below for a `loc` entry,
    // which §9.6 makes a template by definition.
    DiagnosticSink quiet;

    for (const Statement& statement : file.statements()) {
        const std::optional<std::string> key = statement.key_name();
        const std::optional<Value> value = statement.value();
        if (!key || !value) {
            continue;
        }
        if (*key == kLocForm) {
            if (const std::optional<Block> block = value->as_block()) {
                read_loc(*block, first_entry, sink);
            }
            continue;
        }
        if (*key == kStyleForm) {
            if (const std::optional<Block> block = value->as_block()) {
                read_style(*block);
            }
            // And then walked like anything else: a style's own `doc` is a
            // `text` value and so a template, with no exemption from §9.3.
        }
        walk(*value, quiet);
    }
}

void TextIndex::read_style(const Block& block) {
    const std::optional<Value> id = block.value_of(kIdKey);
    const std::optional<Scalar> scalar = id ? id->as_scalar() : std::nullopt;
    const std::optional<std::string_view> name = scalar ? scalar->as_identifier() : std::nullopt;
    if (!name) {
        return; // E-KEY-MISSING or E-TYPE-MISMATCH already said so
    }
    styles_.push_back(Style{std::string(*name), scalar->span()});
}

void TextIndex::read_loc(const Block& block, std::size_t first_entry, DiagnosticSink& sink) {
    const std::optional<Value> declared = block.value_of(kLangKey);
    const std::optional<Scalar> language = declared ? declared->as_scalar() : std::nullopt;
    const std::string lang(language ? language->as_identifier().value_or("") : "");

    for (const Statement& statement : block.statements()) {
        const std::optional<std::string> key = statement.key_name();
        if (!key || *key == kLangKey) {
            continue;
        }

        for (std::size_t i = first_entry; i < entries_.size(); ++i) {
            if (entries_[i].lang != lang || entries_[i].key != *key) {
                continue;
            }
            Diagnostic diagnostic(Code::LocDuplicate, statement.report_span(),
                                  "'" + *key + "' is defined twice in " +
                                      (lang.empty() ? "this locale" : lang));
            diagnostic.with_note("localisation keys are unique within a language, so one of "
                                 "these two is silently unreachable (spec §9.6)",
                                 entries_[i].span);
            sink.report(std::move(diagnostic));
            break;
        }

        entries_.push_back(LocEntry{lang, *key, statement.report_span()});

        // §9.6's values are templates, which is the one place outside the
        // type checker where that is known without asking a schema: a `loc`
        // table holds nothing else.
        const std::optional<Value> value = statement.value();
        const std::optional<Scalar> scalar = value ? value->as_scalar() : std::nullopt;
        if (value) {
            collect_annotations(*value);
        }
        if (!scalar) {
            continue;
        }
        const stardata::text::Template parsed = stardata::text::parse_template(*scalar, sink);
        for (const stardata::text::Fragment* directive : parsed.style_directives()) {
            style_references_.push_back(Reference{directive->text, directive->span});
        }
    }
}

void TextIndex::collect_annotations(const Value& value) {
    for (const Annotation& annotation : value.annotations()) {
        if (annotation.name() != kStyleAnnotation) {
            continue;
        }
        const std::vector<stardata::cst::SyntaxToken> arguments = annotation.arguments();
        if (arguments.empty()) {
            continue; // E-ANNOT-ARGUMENT already said so
        }
        style_references_.push_back(Reference{std::string(arguments.front().text()),
                                              span_of(arguments.front(), annotation.source())});
    }
}

void TextIndex::walk(const Value& value, DiagnosticSink& quiet) {
    collect_annotations(value);

    if (const std::optional<Scalar> scalar = value.as_scalar()) {
        if (const std::optional<std::string_view> key = scalar->as_loc_key()) {
            loc_references_.push_back(Reference{std::string(*key), scalar->span()});
            return;
        }
        const stardata::text::Template parsed = stardata::text::parse_template(*scalar, quiet);
        for (const stardata::text::Fragment* directive : parsed.style_directives()) {
            style_references_.push_back(Reference{directive->text, directive->span});
        }
        return;
    }

    // §4.3's calls in value positions. Their arguments are scalars or nested
    // calls, and a `$key` is as legal there as anywhere a `text` value is.
    if (const std::optional<stardata::ast::Call> call = value.as_call()) {
        for (const stardata::cst::SyntaxNode& argument : call->arguments()) {
            if (const std::optional<Scalar> scalar = Scalar::cast(argument, call->source())) {
                if (const std::optional<std::string_view> key = scalar->as_loc_key()) {
                    loc_references_.push_back(Reference{std::string(*key), scalar->span()});
                }
            }
        }
        return;
    }

    if (const std::optional<Block> block = value.as_block()) {
        for (const Statement& statement : block->statements()) {
            if (const std::optional<Value> inner = statement.value()) {
                walk(*inner, quiet);
            }
        }
        for (const Scalar& entry : block->values()) {
            if (const std::optional<std::string_view> key = entry.as_loc_key()) {
                loc_references_.push_back(Reference{std::string(*key), entry.span()});
                continue;
            }
            const stardata::text::Template parsed = stardata::text::parse_template(entry, quiet);
            for (const stardata::text::Fragment* directive : parsed.style_directives()) {
                style_references_.push_back(Reference{directive->text, directive->span});
            }
        }
    }
}

void TextIndex::check(DiagnosticSink& sink) const {
    const std::vector<std::string_view> declared = names_of(styles_);
    std::set<std::string> known;
    for (const Style& style : styles_) {
        known.insert(style.name);
    }
    for (const Reference& reference : style_references_) {
        if (known.contains(reference.name)) {
            continue;
        }
        Diagnostic diagnostic(Code::StyleUndeclared, reference.span,
                              "nothing declares the style '" + reference.name + "'");
        diagnostic.with_note("styling is semantic: an author names a style and the active theme "
                             "maps it, so a style has to be declared with `style = { id = ... }` "
                             "before it can be named (spec §9.3)");
        stardata::schema::suggest(diagnostic, reference.span, reference.name, declared);
        sink.report(std::move(diagnostic));
    }

    std::set<std::string> defined;
    std::vector<std::string_view> keys;
    for (const LocEntry& entry : entries_) {
        if (defined.insert(entry.key).second) {
            keys.emplace_back(entry.key);
        }
    }

    std::set<std::string> referenced;
    for (const Reference& reference : loc_references_) {
        referenced.insert(reference.name);
        if (defined.contains(reference.name)) {
            continue;
        }
        Diagnostic diagnostic(Code::LocUndefined, reference.span,
                              "no loc table defines '$" + reference.name + "'");
        diagnostic.with_note("a $key resolves against the loaded locale, then the project's "
                             "declared source language, and finally renders visibly as "
                             "«" +
                             reference.name +
                             "» so that a missing string shows up in "
                             "play rather than blank (spec §9.6)");
        stardata::schema::suggest(diagnostic, reference.span, reference.name, keys);
        sink.report(std::move(diagnostic));
    }

    // Once per key, not once per entry. A key present in three translations
    // and referenced by nothing is one unused string, and a project adding a
    // language would otherwise watch each of its warnings multiply.
    std::set<std::string> warned;
    for (const LocEntry& entry : entries_) {
        if (referenced.contains(entry.key) || !warned.insert(entry.key).second) {
            continue;
        }
        Diagnostic diagnostic(Code::LocUnused, entry.span,
                              "nothing references '$" + entry.key + "'");
        diagnostic.with_note("inline strings are assigned generated keys at compile time "
                             "(spec §9.6), so a key written out by hand exists in order to be "
                             "referenced -- an unreferenced one is usually a renamed reference "
                             "or a string that moved");
        sink.report(std::move(diagnostic));
    }
}

} // namespace starcore
