// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/diag/diagnostic.hpp"

#include <utility>

namespace stardata::diag {

Diagnostic::Diagnostic(Code code, Severity severity, Span primary_span, std::string message)
    : code_(code), severity_(severity), primary_span_(primary_span), message_(std::move(message)) {}

Diagnostic::Diagnostic(Code code, Span primary_span, std::string message)
    : Diagnostic(code, default_severity(code), primary_span, std::move(message)) {}

Diagnostic& Diagnostic::with_note(std::string message, std::optional<Span> span) {
    notes_.push_back(Note{std::move(message), span});
    return *this;
}

Diagnostic& Diagnostic::with_fix_it(Span span, std::string replacement, std::string message) {
    fix_its_.push_back(FixIt{span, std::move(replacement), std::move(message)});
    return *this;
}

} // namespace stardata::diag
