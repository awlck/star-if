// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::diag {

// A secondary location cited by a diagnostic, e.g. "first occurrence here"
// on the earlier of the two spans in a duplicate-key error (spec §5.3,
// which requires citing both). `span` is optional so a note can also be
// plain text with no source location of its own.
struct Note {
    std::string message;
    std::optional<Span> span;
};

// A machine-applicable suggestion: replace `span` with `replacement`.
// `message` is the human-readable description shown alongside it.
struct FixIt {
    Span span;
    std::string replacement;
    std::string message;
};

// One diagnostic. Spec §14.3 requires a code, severity and primary span on
// every diagnostic, and a fix-it where one is well defined; §5.3's
// duplicate-key case is why notes carry their own span rather than being
// plain strings -- a diagnostic that can only cite one location cannot
// report it.
class Diagnostic {
public:
    Diagnostic(Code code, Severity severity, Span primary_span, std::string message);

    // Convenience overload: severity defaults to codes.hpp's table.
    Diagnostic(Code code, Span primary_span, std::string message);

    Diagnostic& with_note(std::string message, std::optional<Span> span = std::nullopt);
    Diagnostic& with_fix_it(Span span, std::string replacement, std::string message);

    [[nodiscard]] Code code() const noexcept { return code_; }
    [[nodiscard]] Severity severity() const noexcept { return severity_; }
    [[nodiscard]] const Span& primary_span() const noexcept { return primary_span_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] const std::vector<Note>& notes() const noexcept { return notes_; }
    [[nodiscard]] const std::vector<FixIt>& fix_its() const noexcept { return fix_its_; }

private:
    Code code_;
    Severity severity_;
    Span primary_span_;
    std::string message_;
    std::vector<Note> notes_;
    std::vector<FixIt> fix_its_;
};

} // namespace stardata::diag
