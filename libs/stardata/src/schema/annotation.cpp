// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/annotation.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <exception>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/schema/schema.hpp"

namespace stardata::schema {

namespace {

using diag::Code;
using diag::Diagnostic;

// The frontends §5.4.1 lists for `@platform`. A closed set, because the
// engine selects among alternatives by matching the running frontend against
// these names, and a name no frontend will ever report is an alternative
// that can never be selected -- a statement the author wrote and no session
// will run. A new frontend is a specification edit, which is the same bar
// `qt` and `glk` cleared.
constexpr std::string_view kFrontends[] = {"qt", "web", "glk", "cli", "mobile"};

// §15's reserved annotations. Rejected specifically rather than as unknowns,
// for the reason §15 gives about `?=`: a file written against a draft that
// had them deserves to be told what happened.
constexpr std::string_view kReserved[] = {"deprecated", "since", "experimental"};

[[nodiscard]] bool is_reserved(std::string_view name) noexcept {
    return std::find(std::begin(kReserved), std::end(kReserved), name) != std::end(kReserved);
}

// The shape of a value, in the terms of §5.4.1's "Applies to" column. That
// column has three words in it -- block, string, scalar -- and these are
// they.
enum class Shape : std::uint8_t {
    Block,  // `{ ... }`
    String, // a string literal or a $localisation key: text, either way
    Scalar, // any other single value
};

[[nodiscard]] Shape shape_of(const ast::Value& value) {
    if (value.as_block()) {
        return Shape::Block;
    }
    if (const std::optional<ast::Scalar> scalar = value.as_scalar()) {
        const std::optional<cst::SyntaxKind> kind = scalar->literal_kind();
        if (kind == cst::SyntaxKind::String || kind == cst::SyntaxKind::LocKey) {
            return Shape::String;
        }
    }
    return Shape::Scalar;
}

[[nodiscard]] std::string_view describe(Shape shape) noexcept {
    switch (shape) {
    case Shape::Block:
        return "a block";
    case Shape::String:
        return "a string";
    case Shape::Scalar:
        break;
    }
    return "a single value";
}

// §5.4.1's "Applies to" column, read back. `@override`, `@debug` and
// `@platform` take any shape and so never appear here as a refusal.
[[nodiscard]] bool applies_to(AnnotationKind kind, Shape shape) noexcept {
    switch (kind) {
    case AnnotationKind::Before:
    case AnnotationKind::After:
        return shape == Shape::Block || shape == Shape::String;
    case AnnotationKind::Merge:
    case AnnotationKind::Remove:
    case AnnotationKind::Priority:
        return shape == Shape::Block;
    case AnnotationKind::Style:
        return shape == Shape::String;
    case AnnotationKind::Override:
    case AnnotationKind::Debug:
    case AnnotationKind::Platform:
    case AnnotationKind::Replaces:
        return true;
    }
    return true;
}

// What §5.4.1's column says the annotation takes, for the diagnostic.
[[nodiscard]] std::string_view takes(AnnotationKind kind) noexcept {
    switch (kind) {
    case AnnotationKind::Before:
    case AnnotationKind::After:
        return "a block or a string";
    case AnnotationKind::Merge:
    case AnnotationKind::Remove:
    case AnnotationKind::Priority:
        return "a block";
    case AnnotationKind::Style:
        return "a string";
    default:
        break;
    }
    return "any value";
}

[[nodiscard]] bool is_combining(AnnotationKind kind) noexcept {
    return kind == AnnotationKind::Before || kind == AnnotationKind::After ||
           kind == AnnotationKind::Override || kind == AnnotationKind::Merge ||
           kind == AnnotationKind::Remove;
}

[[nodiscard]] Combination::Mode mode_of(AnnotationKind kind) noexcept {
    switch (kind) {
    case AnnotationKind::Before:
        return Combination::Mode::Before;
    case AnnotationKind::After:
        return Combination::Mode::After;
    case AnnotationKind::Merge:
        return Combination::Mode::Merge;
    case AnnotationKind::Remove:
        return Combination::Mode::Remove;
    default:
        break;
    }
    return Combination::Mode::Override;
}

[[nodiscard]] diag::Span span_of(const ast::Annotation& annotation,
                                 const cst::SyntaxToken& token) noexcept {
    const cst::TextRange range = token.text_range();
    return diag::Span{annotation.source(), range.offset, range.length};
}

[[nodiscard]] std::string spelling(std::string_view name) {
    return "'@" + std::string(name) + "'";
}

// The list of annotations, for the diagnostic that has to name them all.
[[nodiscard]] std::string every_annotation() {
    return "@before, @after, @override, @merge, @remove, @priority, @debug, @platform, @style "
           "and @replaces";
}

[[nodiscard]] std::string every_frontend() {
    std::string text;
    for (std::size_t i = 0; i < std::size(kFrontends); ++i) {
        if (i != 0) {
            text += i + 1 == std::size(kFrontends) ? " and " : ", ";
        }
        text += kFrontends[i];
    }
    return text;
}

// --- one annotation ----------------------------------------------------

// §3.8 and §5.4.1's argument rules. Each annotation takes a fixed shape of
// argument list, and getting it wrong is not a thing to shrug at: an
// `@priority` with no number silently orders nothing, and a `@platform`
// naming a frontend that does not exist strips the statement from every
// build there is.
void check_arguments(const ast::Annotation& annotation, AnnotationKind kind, std::string_view name,
                     diag::DiagnosticSink& sink) {
    const std::vector<cst::SyntaxToken> arguments = annotation.arguments();

    const auto wrong_count = [&](std::string_view wanted) {
        Diagnostic diagnostic(Code::AnnotationArgument, annotation.span(),
                              spelling(name) + " takes " + std::string(wanted) +
                                  ", and this one "
                                  "has " +
                                  (arguments.empty() ? "none" : std::to_string(arguments.size())));
        diagnostic.with_note("spec §3.8 gives an annotation's arguments, and §5.4.1 says what "
                             "each annotation does with them");
        sink.report(std::move(diagnostic));
    };

    switch (kind) {
    case AnnotationKind::Before:
    case AnnotationKind::After:
    case AnnotationKind::Override:
    case AnnotationKind::Merge:
    case AnnotationKind::Remove:
    case AnnotationKind::Debug:
        if (!arguments.empty()) {
            Diagnostic diagnostic(Code::AnnotationArgument, annotation.span(),
                                  spelling(name) + " takes no arguments");
            diagnostic.with_note("only @priority, @platform, @style and @replaces do (spec "
                                 "§5.4.1)");
            sink.report(std::move(diagnostic));
        }
        return;

    case AnnotationKind::Priority:
        // `@priority(n)`: "n is an integer, default 0, higher runs first".
        if (arguments.size() != 1) {
            wrong_count("one whole number");
            return;
        }
        if (arguments.front().kind() != cst::SyntaxKind::Integer) {
            Diagnostic diagnostic(Code::AnnotationArgument, span_of(annotation, arguments.front()),
                                  spelling(name) + " orders by a whole number, and '" +
                                      std::string(arguments.front().text()) + "' is not one");
            diagnostic.with_note("a higher priority runs first, and the default is 0 (spec "
                                 "§5.4.1)");
            sink.report(std::move(diagnostic));
        }
        return;

    case AnnotationKind::Platform:
        if (arguments.empty()) {
            Diagnostic diagnostic(Code::AnnotationArgument, annotation.span(),
                                  spelling(name) + " names the frontends this is selected on, and "
                                                   "this one names none");
            diagnostic.with_note("no frontend matches an empty list, so the statement would ship "
                                 "and never run; omit the annotation to have it on all of them "
                                 "(spec §5.4.1)");
            sink.report(std::move(diagnostic));
            return;
        }
        for (const cst::SyntaxToken& argument : arguments) {
            if (is_frontend(argument.text())) {
                continue;
            }
            Diagnostic diagnostic(Code::AnnotationArgument, span_of(annotation, argument),
                                  "'" + std::string(argument.text()) +
                                      "' is not a frontend I know");
            diagnostic.with_note("the frontends are " + every_frontend() +
                                 ", and the running one "
                                 "declares itself at session start -- an alternative gated on a "
                                 "name none of them reports can never be selected (spec §5.4.1)");
            sink.report(std::move(diagnostic));
        }
        return;

    case AnnotationKind::Style:
    case AnnotationKind::Replaces:
        // One identifier each: a style name (§9.3) and a library id (§7.6).
        // Whether either names something that exists is somebody else's
        // check -- F7's and SchemaSet::offer's respectively -- and this is
        // only that there is one name to look up.
        if (arguments.size() != 1) {
            wrong_count(kind == AnnotationKind::Style ? "one style name" : "one library id");
            return;
        }
        if (arguments.front().kind() != cst::SyntaxKind::Identifier) {
            Diagnostic diagnostic(Code::AnnotationArgument, span_of(annotation, arguments.front()),
                                  spelling(name) + " takes a name, and '" +
                                      std::string(arguments.front().text()) + "' is a number");
            diagnostic.with_note("spec §3.8: an annotation argument is a name or an integer, and "
                                 "this one has to be a name");
            sink.report(std::move(diagnostic));
        }
        return;
    }
}

// Everything §5.4 says about one value's annotations, taken together: each
// is defined, each applies here, each is spelled with the right arguments,
// and no two of them combine the value differently.
void check_value_annotations(const ast::Value& value, bool top_level, diag::DiagnosticSink& sink) {
    const Shape shape = shape_of(value);

    // The first combining annotation seen, so a second can cite it. §5.4
    // says annotations are processed left to right and that contradictory
    // combinations are rejected; the first is what a reader takes the value
    // to do, so it is the one a diagnostic should point back at.
    std::optional<AnnotationKind> combining;
    std::optional<diag::Span> combining_span;

    for (const ast::Annotation& annotation : value.annotations()) {
        const std::optional<std::string_view> name = annotation.name();
        if (!name || name->empty()) {
            continue; // '@' with nothing after it; the lexer said so already
        }

        const std::optional<AnnotationKind> kind = annotation_from_string(*name);
        if (!kind) {
            Diagnostic diagnostic(Code::UnknownAnnotation, annotation.span(),
                                  is_reserved(*name)
                                      ? spelling(*name) + " is reserved, and means nothing yet"
                                      : spelling(*name) + " is not an annotation I know");
            if (is_reserved(*name)) {
                diagnostic.with_note("spec §15 holds @deprecated, @since and @experimental back "
                                     "so that defining one later is not a breaking change -- "
                                     "which only works if writing one today is refused");
            } else {
                diagnostic.with_note("the annotations are " + every_annotation() +
                                     " (spec §5.4.1)");
            }
            diagnostic.with_note("an unknown annotation is an error rather than something to "
                                 "ignore, because ignoring one changes what the value means with "
                                 "nothing on screen to say so (spec §3.8)");
            sink.report(std::move(diagnostic));
            continue;
        }

        check_arguments(annotation, *kind, *name, sink);

        // §7.6: `@replaces` supersedes a whole top-level declaration. On a
        // key inside a block it would be claiming to replace something that
        // has no id to be replaced by.
        if (*kind == AnnotationKind::Replaces && !top_level) {
            Diagnostic diagnostic(Code::AnnotationMisapplied, annotation.span(),
                                  spelling(*name) +
                                      " supersedes a whole declaration, and this is a key inside "
                                      "one");
            diagnostic.with_note("a key's value is combined with what it inherits by @override, "
                                 "@merge and the rest; superseding a declaration by id is what "
                                 "@replaces does, at the top level (spec §5.4.1, §7.6)");
            sink.report(std::move(diagnostic));
        }

        if (!applies_to(*kind, shape)) {
            Diagnostic diagnostic(Code::AnnotationMisapplied, annotation.span(),
                                  spelling(*name) + " applies to " + std::string(takes(*kind)) +
                                      ", and this value is " + std::string(describe(shape)));
            diagnostic.with_note("§5.4.1 gives each annotation what it applies to, and an "
                                 "annotation written where it cannot act is one an author has "
                                 "every reason to think is working");
            sink.report(std::move(diagnostic));
        }

        if (!is_combining(*kind)) {
            continue;
        }
        if (!combining) {
            combining = *kind;
            combining_span = annotation.span();
            continue;
        }

        Diagnostic diagnostic(
            Code::AnnotationConflict, annotation.span(),
            *combining == *kind ? spelling(*name) + " is written on this value twice"
                                : "this value is combined two ways, " +
                                      spelling(to_string(*combining)) + " and " + spelling(*name));
        diagnostic.with_note(spelling(to_string(*combining)) + " is here", combining_span);
        diagnostic.with_note("each of @before, @after, @override, @merge and @remove says what "
                             "this value does to the one it inherits, so a value carries at most "
                             "one of them (spec §5.4, §5.4.1)");
        sink.report(std::move(diagnostic));
    }
}

void visit(const ast::Statement& statement, bool top_level, diag::DiagnosticSink& sink) {
    const std::optional<ast::Value> value = statement.value();
    if (!value) {
        return;
    }
    check_value_annotations(*value, top_level, sink);

    if (const std::optional<ast::Block> block = value->as_block()) {
        for (const ast::Statement& nested : block->statements()) {
            visit(nested, /*top_level=*/false, sink);
        }
    }
}

} // namespace

std::string_view to_string(AnnotationKind kind) noexcept {
    switch (kind) {
    case AnnotationKind::Before:
        return "before";
    case AnnotationKind::After:
        return "after";
    case AnnotationKind::Override:
        return "override";
    case AnnotationKind::Merge:
        return "merge";
    case AnnotationKind::Remove:
        return "remove";
    case AnnotationKind::Priority:
        return "priority";
    case AnnotationKind::Debug:
        return "debug";
    case AnnotationKind::Platform:
        return "platform";
    case AnnotationKind::Style:
        return "style";
    case AnnotationKind::Replaces:
        return "replaces";
    }
    assert(false && "unhandled AnnotationKind");
    return "";
}

std::optional<AnnotationKind> annotation_from_string(std::string_view name) noexcept {
    if (name == "before") {
        return AnnotationKind::Before;
    }
    if (name == "after") {
        return AnnotationKind::After;
    }
    if (name == "override") {
        return AnnotationKind::Override;
    }
    if (name == "merge") {
        return AnnotationKind::Merge;
    }
    if (name == "remove") {
        return AnnotationKind::Remove;
    }
    if (name == "priority") {
        return AnnotationKind::Priority;
    }
    if (name == "debug") {
        return AnnotationKind::Debug;
    }
    if (name == "platform") {
        return AnnotationKind::Platform;
    }
    if (name == "style") {
        return AnnotationKind::Style;
    }
    if (name == "replaces") {
        return AnnotationKind::Replaces;
    }
    return std::nullopt;
}

bool is_frontend(std::string_view name) noexcept {
    return std::find(std::begin(kFrontends), std::end(kFrontends), name) != std::end(kFrontends);
}

std::string_view to_string(Combination::Mode mode) noexcept {
    switch (mode) {
    case Combination::Mode::Before:
        return "before";
    case Combination::Mode::After:
        return "after";
    case Combination::Mode::Override:
        return "override";
    case Combination::Mode::Merge:
        return "merge";
    case Combination::Mode::Remove:
        return "remove";
    }
    assert(false && "unhandled Combination::Mode");
    return "override";
}

Combination combination_of(const ast::Statement& statement, const KeyDecl* declared) {
    Combination result;
    result.span = statement.report_span();

    const std::optional<ast::Value> value = statement.value();
    if (!value) {
        return result;
    }

    // §5.4.2's table, which is what applies when no annotation says
    // otherwise. `smart` is the row that has to look at the value: an empty
    // block is an explicit override with no content (§5.2), and anything
    // else appends -- which is the rule proposal §7.2 stated behaviourally
    // for `restrictions` and `conditions` before §5.4.2 named it.
    const Combine mode = declared != nullptr ? declared->combine : Combine::Override;
    switch (mode) {
    case Combine::Override:
        result.mode = Combination::Mode::Override;
        break;
    case Combine::Merge:
        result.mode = Combination::Mode::Merge;
        break;
    case Combine::Append:
        result.mode = Combination::Mode::After;
        break;
    case Combine::Smart: {
        const std::optional<ast::Block> block = value->as_block();
        result.mode =
            block && block->is_empty() ? Combination::Mode::Override : Combination::Mode::After;
        break;
    }
    }

    // Then the annotations, left to right (§5.4). A second combining
    // annotation is E-ANNOT-CONFLICT and is reported by check_annotations;
    // here the first one wins, so that a file with that error still has a
    // defined reading rather than whichever the loop happened to end on.
    for (const ast::Annotation& annotation : value->annotations()) {
        const std::optional<std::string_view> name = annotation.name();
        const std::optional<AnnotationKind> kind =
            name ? annotation_from_string(*name) : std::nullopt;
        if (!kind) {
            continue;
        }
        if (*kind == AnnotationKind::Priority) {
            const std::vector<cst::SyntaxToken> arguments = annotation.arguments();
            if (arguments.size() == 1 && arguments.front().kind() == cst::SyntaxKind::Integer) {
                try {
                    result.priority = std::stoll(std::string(arguments.front().text()));
                } catch (const std::exception&) {
                    // Out of range or unreadable. The lexer reports an
                    // integer it cannot hold (E-INT-RANGE); leaving the
                    // priority at its default here beats propagating a
                    // number nobody wrote.
                }
            }
            continue;
        }
        if (!is_combining(*kind) || result.annotated) {
            continue;
        }
        result.mode = mode_of(*kind);
        result.annotated = true;
        result.span = annotation.span();
    }
    return result;
}

bool Presence::can_coexist_with(const Presence& other) const {
    // `debug_only` is deliberately not consulted. It narrows which builds a
    // statement reaches and never excludes another statement from those, so
    // it can only ever be the frontends that pull two statements apart.
    if (frontends.empty() || other.frontends.empty()) {
        return true; // one runs on every frontend, so it meets the other on all of theirs
    }
    for (const std::string& frontend : frontends) {
        if (std::find(other.frontends.begin(), other.frontends.end(), frontend) !=
            other.frontends.end()) {
            return true;
        }
    }
    return false;
}

Presence presence_of(const ast::Statement& statement) {
    Presence presence;
    const std::optional<ast::Value> value = statement.value();
    if (!value) {
        return presence;
    }
    for (const ast::Annotation& annotation : value->annotations()) {
        const std::optional<std::string_view> name = annotation.name();
        const std::optional<AnnotationKind> kind =
            name ? annotation_from_string(*name) : std::nullopt;
        if (kind == AnnotationKind::Debug) {
            presence.debug_only = true;
            presence.span = annotation.span();
            continue;
        }
        if (kind != AnnotationKind::Platform) {
            continue;
        }
        presence.span = annotation.span();
        for (const cst::SyntaxToken& argument : annotation.arguments()) {
            if (argument.kind() == cst::SyntaxKind::Identifier) {
                presence.frontends.emplace_back(argument.text());
            }
        }
    }
    return presence;
}

void check_annotations(const ast::File& file, diag::DiagnosticSink& sink) {
    for (const ast::Statement& statement : file.statements()) {
        visit(statement, /*top_level=*/true, sink);
    }
}

} // namespace stardata::schema
