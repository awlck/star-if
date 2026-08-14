// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <vector>

#include "stardata/diag/diagnostic.hpp"

namespace stardata::diag {

// Collects diagnostics as a pass reports them, and tracks per-severity
// counts so a caller can decide an exit code without re-scanning the
// collected list.
//
// `limit` (0 = unlimited) bounds how many diagnostics are kept: past the
// limit, `report` still counts the diagnostic towards its severity but
// drops it instead of storing it, so a single cascading error cannot make
// rendering ten thousand near-duplicate diagnostics the bottleneck.
class DiagnosticSink {
public:
    explicit DiagnosticSink(std::size_t limit = 0) noexcept : limit_(limit) {}

    void report(Diagnostic diagnostic);

    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }
    [[nodiscard]] std::size_t error_count() const noexcept { return error_count_; }
    [[nodiscard]] std::size_t warning_count() const noexcept { return warning_count_; }
    [[nodiscard]] std::size_t info_count() const noexcept { return info_count_; }
    [[nodiscard]] std::size_t suppressed_count() const noexcept { return suppressed_count_; }
    [[nodiscard]] bool has_errors() const noexcept { return error_count_ > 0; }

private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t limit_;
    std::size_t error_count_ = 0;
    std::size_t warning_count_ = 0;
    std::size_t info_count_ = 0;
    std::size_t suppressed_count_ = 0;
};

} // namespace stardata::diag
