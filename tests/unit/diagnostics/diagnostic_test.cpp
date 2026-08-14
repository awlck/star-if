// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include <catch2/catch_test_macros.hpp>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

using namespace stardata::diag;

namespace {
Span make_span(SourceId id, std::uint32_t offset, std::uint32_t length) {
    return Span{id, offset, length};
}
} // namespace

TEST_CASE("code_string returns a stable string for every code", "[diag][diagnostic]") {
    CHECK(code_string(Code::DuplicateKey) == "E-DUP-KEY");
    CHECK(code_string(Code::FailmsgMissing) == "W-FAILMSG-MISSING");
    CHECK(code_string(Code::TraitCountHigh) == "I-TRAIT-COUNT");
}

TEST_CASE("default_severity matches spec's required-diagnostics table", "[diag][diagnostic]") {
    CHECK(default_severity(Code::DuplicateKey) == Severity::Error);
    CHECK(default_severity(Code::FailmsgMissing) == Severity::Warning);
    CHECK(default_severity(Code::TraitCountHigh) == Severity::Info);
}

TEST_CASE("a two-argument Diagnostic uses the code's default severity", "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "title = \"B\"");
    Diagnostic diag(Code::FailmsgMissing, make_span(id, 0, 5), "no reachable failureMsg");

    CHECK(diag.severity() == Severity::Warning);
    CHECK(diag.code() == Code::FailmsgMissing);
    CHECK(diag.message() == "no reachable failureMsg");
}

TEST_CASE("an explicit severity overrides the code's default", "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    Diagnostic diag(Code::FailmsgMissing, Severity::Error, make_span(id, 0, 1), "escalated");

    CHECK(diag.severity() == Severity::Error);
}

TEST_CASE("with_note attaches a secondary span, as the duplicate-key case requires",
          "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("dup.star", "title = \"A\"\n"
                                                 "title = \"B\"\n");
    const Span first = make_span(id, 0, 5);
    const Span second = make_span(id, 12, 5);

    Diagnostic diag(Code::DuplicateKey, second, "duplicate key 'title' (arity = one)");
    diag.with_note("first occurrence here", first);

    REQUIRE(diag.notes().size() == 1);
    REQUIRE(diag.notes()[0].span.has_value());
    CHECK(diag.notes()[0].span->offset == first.offset);
    CHECK(diag.notes()[0].message == "first occurrence here");
}

TEST_CASE("with_note without a span is plain text", "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    Diagnostic diag(Code::GlobalUnused, make_span(id, 0, 1), "declared but never read");
    diag.with_note("consider removing the declaration");

    REQUIRE(diag.notes().size() == 1);
    CHECK_FALSE(diag.notes()[0].span.has_value());
}

TEST_CASE("with_fix_it attaches a replacement suggestion", "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "has_hat");
    Diagnostic diag(Code::PropMaybeAbsent, make_span(id, 0, 7), "'hat' possibly absent");
    diag.with_fix_it(make_span(id, 0, 0), "has_prop(hat) AND ", "guard with has_prop");

    REQUIRE(diag.fix_its().size() == 1);
    CHECK(diag.fix_its()[0].replacement == "has_prop(hat) AND ");
}

TEST_CASE("with_note and with_fix_it chain off the same diagnostic", "[diag][diagnostic]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    const Span span = make_span(id, 0, 1);

    Diagnostic diag(Code::DuplicateKey, span, "duplicate key");
    diag.with_note("also here", span).with_fix_it(span, "", "remove one");

    CHECK(diag.notes().size() == 1);
    CHECK(diag.fix_its().size() == 1);
}

TEST_CASE("DiagnosticSink counts diagnostics by severity", "[diag][sink]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    DiagnosticSink sink;

    sink.report(Diagnostic(Code::DuplicateKey, make_span(id, 0, 1), "e1"));
    sink.report(Diagnostic(Code::UnknownKey, make_span(id, 0, 1), "e2"));
    sink.report(Diagnostic(Code::FailmsgMissing, make_span(id, 0, 1), "w1"));

    CHECK(sink.error_count() == 2);
    CHECK(sink.warning_count() == 1);
    CHECK(sink.info_count() == 0);
    CHECK(sink.has_errors());
    CHECK(sink.diagnostics().size() == 3);
    CHECK(sink.suppressed_count() == 0);
}

TEST_CASE("DiagnosticSink drops diagnostics past its limit but keeps counting them",
          "[diag][sink]") {
    SourceManager sources;
    const auto id = sources.add_file("x.star", "x");
    DiagnosticSink sink(/*limit=*/2);

    sink.report(Diagnostic(Code::DuplicateKey, make_span(id, 0, 1), "e1"));
    sink.report(Diagnostic(Code::UnknownKey, make_span(id, 0, 1), "e2"));
    sink.report(Diagnostic(Code::UnknownAnnotation, make_span(id, 0, 1), "e3"));

    CHECK(sink.diagnostics().size() == 2);
    CHECK(sink.error_count() == 3);
    CHECK(sink.suppressed_count() == 1);
}

TEST_CASE("a sink with no diagnostics reports no errors", "[diag][sink]") {
    DiagnosticSink sink;
    CHECK_FALSE(sink.has_errors());
    CHECK(sink.diagnostics().empty());
}
