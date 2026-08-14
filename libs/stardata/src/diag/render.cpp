// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/diag/render.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace stardata::diag {

namespace {

constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kRed = "\x1b[31m";
constexpr std::string_view kYellow = "\x1b[33m";
constexpr std::string_view kCyan = "\x1b[36m";

[[nodiscard]] std::string_view severity_color(Severity severity) noexcept {
    switch (severity) {
    case Severity::Error:
        return kRed;
    case Severity::Warning:
        return kYellow;
    case Severity::Info:
        return kCyan;
    }
    return kReset;
}

[[nodiscard]] unsigned digit_count(std::uint32_t n) noexcept {
    unsigned digits = 1;
    while (n >= 10) {
        n /= 10;
        ++digits;
    }
    return digits;
}

// One source line of context under a span, with a caret underline below it:
//
//   --> path:line:col
//    |
//  9 | title = "B"
//    |   ^^^^^
void print_snippet(std::ostream& out, const SourceManager& sources, const Span& span,
                   bool use_color, std::string_view color) {
    const LineCol lc = sources.line_col(span.source, span.offset);
    const std::string_view line_text = sources.line_text(span.source, lc.line);
    const unsigned gutter = digit_count(lc.line);
    const std::string blank_gutter(gutter, ' ');

    out << "  --> " << sources.path(span.source).string() << ':' << lc.line << ':' << lc.column
        << '\n';
    out << blank_gutter << " |\n";
    out << lc.line << " | " << line_text << '\n';
    out << blank_gutter << " | " << std::string(lc.column - 1, ' ');

    const std::uint32_t width =
        std::max<std::uint32_t>(1, sources.column_width(span.source, span.offset, span.length));
    if (use_color) {
        out << color;
    }
    out << std::string(width, '^');
    if (use_color) {
        out << kReset;
    }
    out << '\n';
}

} // namespace

void render_human(std::ostream& out, const Diagnostic& diagnostic, const SourceManager& sources,
                  bool use_color) {
    const std::string_view color = severity_color(diagnostic.severity());

    if (use_color) {
        out << kBold << color;
    }
    out << to_string(diagnostic.severity());
    if (use_color) {
        out << kReset;
    }
    out << ": " << code_string(diagnostic.code()) << ": " << diagnostic.message() << '\n';
    print_snippet(out, sources, diagnostic.primary_span(), use_color, color);

    for (const Note& note : diagnostic.notes()) {
        out << "note: " << note.message << '\n';
        if (note.span) {
            print_snippet(out, sources, *note.span, use_color, kCyan);
        }
    }

    for (const FixIt& fix : diagnostic.fix_its()) {
        const LineCol lc = sources.line_col(fix.span.source, fix.span.offset);
        out << "fix-it: " << fix.message << '\n';
        out << "  replace " << sources.path(fix.span.source).string() << ':' << lc.line << ':'
            << lc.column << " with `" << fix.replacement << "`\n";
    }
}

void render_machine(std::ostream& out, const Diagnostic& diagnostic, const SourceManager& sources) {
    const Span& span = diagnostic.primary_span();
    const LineCol lc = sources.line_col(span.source, span.offset);
    out << sources.path(span.source).string() << ':' << lc.line << ':' << lc.column << ": "
        << to_string(diagnostic.severity()) << ": " << code_string(diagnostic.code()) << ": "
        << diagnostic.message() << '\n';
}

bool stdout_is_tty() noexcept {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

bool stderr_is_tty() noexcept {
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

} // namespace stardata::diag
