// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// The template grammar of spec §9.1 and §9.2 (backlog F7).
//
// Everything here is about structure, because structure is all this layer
// has. What `the`, `noun` and `verb` mean is starcore's (starcore/text.hpp)
// and what they compile to is Phase 1's; the question asked below is only
// whether the parser sees what an author wrote.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "stardata/cst/parser.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/text/template.hpp"

using namespace stardata;

namespace {

// A template parsed the way the schema layer parses one: out of a real
// statement, so the spans are real spans into a real file.
//
// Through the parser rather than by handing `parse_template` a string,
// because the §3.5.1 run is the case most likely to break: a template that
// opens `[if` in one literal and closes `[end]` in another is one template,
// and only a Scalar knows that.
struct Parsed {
    diag::SourceManager sources;
    diag::SourceId id;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    cst::GreenNodePtr green;
    text::Template result;

    explicit Parsed(const std::string& value) {
        const std::string source = "text = " + value + "\n";
        id = sources.add_file("template.star", source);
        green = cst::parse(sources, id, cache, sink);
        const ast::File file = ast::File::from(cst::SyntaxNode::root(green), id);
        const std::vector<ast::Statement> statements = file.statements();
        REQUIRE(statements.size() == 1);
        const std::optional<ast::Value> held = statements[0].value();
        REQUIRE(held);
        const std::optional<ast::Scalar> scalar = held->as_scalar();
        REQUIRE(scalar);
        result = text::parse_template(*scalar, sink);
    }

    [[nodiscard]] bool reported(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (diagnostic.code() == code) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string underlined(std::size_t index) const {
        return std::string(sources.text(sink.diagnostics()[index].primary_span()));
    }
};

[[nodiscard]] std::vector<text::Fragment::Kind> kinds(const text::Template& parsed) {
    std::vector<text::Fragment::Kind> found;
    found.reserve(parsed.fragments.size());
    for (const text::Fragment& fragment : parsed.fragments) {
        found.push_back(fragment.kind);
    }
    return found;
}

} // namespace

TEST_CASE("plain text is one literal fragment", "[text][template]") {
    const Parsed parsed("\"You open the door.\"");
    REQUIRE(parsed.result.fragments.size() == 1);
    CHECK(parsed.result.fragments[0].kind == text::Fragment::Kind::Literal);
    CHECK(parsed.result.fragments[0].text == "You open the door.");
    CHECK(parsed.sink.diagnostics().empty());
}

TEST_CASE("an interpolation separates the literals around it", "[text][template]") {
    const Parsed parsed("\"You open [the noun] carefully.\"");
    REQUIRE(kinds(parsed.result) == std::vector{text::Fragment::Kind::Literal,
                                                text::Fragment::Kind::Interpolation,
                                                text::Fragment::Kind::Literal});
    CHECK(parsed.result.fragments[0].text == "You open ");
    CHECK(parsed.result.fragments[2].text == " carefully.");
}

TEST_CASE("juxtaposition is single-argument application", "[text][template]") {
    // §9.2.1: "`[the noun]` is sugar for `[the(noun)]`", and the rule is
    // general rather than a special form for articles.
    const Parsed sugar("\"[the noun]\"");
    REQUIRE(sugar.result.fragments.size() == 1);
    const std::optional<text::Expr>& applied = sugar.result.fragments[0].expr;
    REQUIRE(applied);
    CHECK(applied->kind == text::Expr::Kind::Apply);
    CHECK(applied->name == "the");
    REQUIRE(applied->args.size() == 1);
    CHECK(applied->args[0].kind == text::Expr::Kind::Name);
    CHECK(applied->args[0].name == "noun");

    // The parenthesised spelling is the same call. The two are kept
    // distinguishable here -- §14.2's round trip does not stop at the quote
    // -- but they say the same thing.
    const Parsed called("\"[the(noun)]\"");
    const std::optional<text::Expr>& call = called.result.fragments[0].expr;
    REQUIRE(call);
    CHECK(call->kind == text::Expr::Kind::Call);
    CHECK(call->name == "the");
    REQUIRE(call->args.size() == 1);
    CHECK(call->args[0].name == "noun");
}

TEST_CASE("an application nests to the right", "[text][template]") {
    // §9.2.1's own example: `[the HolderOf(noun)]` is `[the(HolderOf(noun))]`.
    const Parsed parsed("\"[the HolderOf(noun)]\"");
    const std::optional<text::Expr>& expr = parsed.result.fragments[0].expr;
    REQUIRE(expr);
    CHECK(expr->kind == text::Expr::Kind::Apply);
    CHECK(expr->name == "the");
    REQUIRE(expr->args.size() == 1);
    CHECK(expr->args[0].kind == text::Expr::Kind::Call);
    CHECK(expr->args[0].name == "HolderOf");
}

TEST_CASE("a call takes any number of arguments", "[text][template]") {
    const Parsed parsed("\"[verb(actor, take)]\"");
    const std::optional<text::Expr>& expr = parsed.result.fragments[0].expr;
    REQUIRE(expr);
    CHECK(expr->kind == text::Expr::Kind::Call);
    REQUIRE(expr->args.size() == 2);
    CHECK(expr->args[0].name == "actor");
    CHECK(expr->args[1].name == "take");
}

TEST_CASE("a path is a head and its segments", "[text][template]") {
    // §3.3 makes `self.range` one Identifier token and says the dot "has no
    // built-in meaning at the lexical level". §9.2 gives it one here, which
    // is why the template layer lexes its own identifiers.
    const Parsed parsed("\"[self.range]\"");
    const std::optional<text::Expr>& expr = parsed.result.fragments[0].expr;
    REQUIRE(expr);
    CHECK(expr->kind == text::Expr::Kind::Path);
    CHECK(expr->name == "self");
    REQUIRE(expr->segments.size() == 1);
    CHECK(expr->segments[0] == "range");
}

TEST_CASE("if, else and end are never read as applications", "[text][template]") {
    // §9.2.1 says so explicitly: they are reserved (§3.9), so `[if is_dark]`
    // stays the conditional of §9.1 rather than becoming `if(is_dark)`.
    const Parsed parsed("\"[if is_dark]dark[else]lit[end]\"");
    REQUIRE(kinds(parsed.result) ==
            std::vector{text::Fragment::Kind::If, text::Fragment::Kind::Literal,
                        text::Fragment::Kind::Else, text::Fragment::Kind::Literal,
                        text::Fragment::Kind::End});
    const std::optional<text::Expr>& condition = parsed.result.fragments[0].expr;
    REQUIRE(condition);
    CHECK(condition->kind == text::Expr::Kind::Name);
    CHECK(condition->name == "is_dark");
}

TEST_CASE("style directives open and close spans", "[text][template]") {
    const Parsed parsed("\"@style(danger)look out@endstyle then\"");
    REQUIRE(kinds(parsed.result) ==
            std::vector{text::Fragment::Kind::StyleOpen, text::Fragment::Kind::Literal,
                        text::Fragment::Kind::StyleClose, text::Fragment::Kind::Literal});
    CHECK(parsed.result.fragments[0].text == "danger");

    const std::vector<const text::Fragment*> directives = parsed.result.style_directives();
    REQUIRE(directives.size() == 1);
    CHECK(directives[0]->text == "danger");
}

TEST_CASE("an at-sign that opens no directive is literal text", "[text][template]") {
    // §9.1: "literal text is everything not otherwise matched". Reporting a
    // near miss would make `@ 10 credits` an error in a message, which is
    // a worse failure than not diagnosing a typo nobody makes.
    const Parsed parsed("\"Costs @ 10 credits, or @styled goods\"");
    REQUIRE(parsed.result.fragments.size() == 1);
    CHECK(parsed.result.fragments[0].kind == text::Fragment::Kind::Literal);
    CHECK(parsed.sink.diagnostics().empty());
}

TEST_CASE("escaped delimiters are literal, not syntax", "[text][template]") {
    // The reason this file's parser reads the token text rather than the
    // decoded string. §3.5 makes `\[` a *string* escape, so by the time
    // `Scalar::as_string()` has run, an escaped bracket and a real one are
    // the same byte -- and §9.1's escape rule would be unimplementable.
    const Parsed parsed("\"a bracket: \\[ and \\] and an at: \\@style(x)\"");
    REQUIRE(parsed.result.fragments.size() == 1);
    CHECK(parsed.result.fragments[0].kind == text::Fragment::Kind::Literal);
    CHECK(parsed.result.fragments[0].text == "a bracket: [ and ] and an at: @style(x)");
    CHECK_FALSE(parsed.reported(diag::Code::TemplateBrackets));
}

TEST_CASE("an unclosed bracket is reported once, at the bracket", "[text][template]") {
    const Parsed parsed("\"You lever [the noun open.\"");
    REQUIRE(parsed.sink.diagnostics().size() == 1);
    CHECK(parsed.reported(diag::Code::TemplateBrackets));
    CHECK(parsed.underlined(0) == "[");
}

TEST_CASE("a closing bracket that opens nothing is reported", "[text][template]") {
    const Parsed parsed("\"It won't budge] no matter what.\"");
    REQUIRE(parsed.sink.diagnostics().size() == 1);
    CHECK(parsed.reported(diag::Code::TemplateBrackets));
    CHECK(parsed.underlined(0) == "]");
}

TEST_CASE("adjacent literals are one template, not several", "[text][template]") {
    // §3.5.1's concatenation, and tour.star's `conditional_demo` is exactly
    // this shape. A per-literal parse would report three unbalanced-bracket
    // errors on a correct file.
    const Parsed parsed("\"[if is_dark]You can't see much.\"\n"
                        "       \"[else]The bay is lit.\"\n"
                        "       \"[end]\"");
    CHECK(parsed.sink.diagnostics().empty());
    REQUIRE(kinds(parsed.result) ==
            std::vector{text::Fragment::Kind::If, text::Fragment::Kind::Literal,
                        text::Fragment::Kind::Else, text::Fragment::Kind::Literal,
                        text::Fragment::Kind::End});
}

TEST_CASE("a malformed expression is silent and carries nothing", "[text][template]") {
    // Deliberate, and recorded as an [OPEN] on backlog F7. §9.2's own MUST is
    // that a name "MUST resolve at compile time to either a template builtin
    // or a Starscript function"; Starscript is §12 and Phase 0 has none of
    // it. Diagnosing the shape of an expression while being unable to say
    // whether it names anything would report the smaller half of the problem
    // and imply the larger half had been checked.
    const Parsed parsed("\"[verb(actor,]\"");
    REQUIRE(parsed.result.fragments.size() == 1);
    CHECK(parsed.result.fragments[0].kind == text::Fragment::Kind::Interpolation);
    CHECK_FALSE(parsed.result.fragments[0].expr.has_value());
    CHECK(parsed.sink.diagnostics().empty());
}

TEST_CASE("capitalisation is one generic rule, not a table of twins", "[text][template]") {
    // §9.2.2. `The`, `A`, `Name` and `Number` are not entries anywhere, and
    // an author-defined script function gets the same treatment for free.
    CHECK(text::name_lookup("the").name == "the");
    CHECK_FALSE(text::name_lookup("the").capitalises);

    CHECK(text::name_lookup("The").name == "the");
    CHECK(text::name_lookup("The").capitalises);

    CHECK(text::name_lookup("Name").name == "name");
    CHECK(text::name_lookup("Name").capitalises);

    // Including for a name nothing here has ever heard of, which is the
    // whole point of the rule being stated once.
    CHECK(text::name_lookup("DamageString").name == "damageString");
    CHECK(text::name_lookup("DamageString").capitalises);

    CHECK(text::name_lookup("").name.empty());
}

TEST_CASE("a localisation key is a reference to a template, not one", "[text][template]") {
    const Parsed parsed("$already_holding");
    CHECK(parsed.result.empty());
    CHECK(parsed.sink.diagnostics().empty());
}
