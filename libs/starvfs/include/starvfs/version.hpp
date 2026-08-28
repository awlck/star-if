// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string_view>

namespace starvfs {

// Mirrors stardata::version() (backlog B1/B3): exists so the build skeleton
// has a real symbol to link and smoke-test against, now that libs/starvfs
// is more than a README. Placeholder until this library has real behaviour
// beyond this change's stub (G2 onward: the directory layer, the zip
// layer, and the mount stack's resolution logic).
[[nodiscard]] std::string_view version() noexcept;

} // namespace starvfs
