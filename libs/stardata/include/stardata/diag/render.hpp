// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <ostream>

#include "stardata/diag/diagnostic.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::diag {

// Renders one diagnostic the way a human reads it: source line, caret,
// underline, notes and fix-its. `use_color` should come from a TTY check at
// the call site (see stdout_is_tty/stderr_is_tty below) -- the renderer
// never probes the stream itself, so it stays testable against an ordinary
// std::ostringstream regardless of how the test binary is invoked.
void render_human(std::ostream& out, const Diagnostic& diagnostic, const SourceManager& sources,
                  bool use_color);

// Renders one diagnostic as a single line:
// `path:line:col: severity: code: message`
// Stable and greppable for CI logs and editor integrations. Never coloured.
void render_machine(std::ostream& out, const Diagnostic& diagnostic, const SourceManager& sources);

// True when the given standard stream is attached to a terminal. Piping or
// redirecting -- which CI always does -- makes this false: colour on a TTY,
// never when piped (backlog C3).
[[nodiscard]] bool stdout_is_tty() noexcept;
[[nodiscard]] bool stderr_is_tty() noexcept;

} // namespace stardata::diag
