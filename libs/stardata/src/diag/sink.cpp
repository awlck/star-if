// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/diag/sink.hpp"

#include <utility>

namespace stardata::diag {

void DiagnosticSink::report(Diagnostic diagnostic) {
    switch (diagnostic.severity()) {
    case Severity::Error:
        ++error_count_;
        break;
    case Severity::Warning:
        ++warning_count_;
        break;
    case Severity::Info:
        ++info_count_;
        break;
    }

    if (limit_ != 0 && diagnostics_.size() >= limit_) {
        ++suppressed_count_;
        return;
    }
    diagnostics_.push_back(std::move(diagnostic));
}

} // namespace stardata::diag
