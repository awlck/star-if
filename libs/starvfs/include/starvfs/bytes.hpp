// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstddef>
#include <vector>

namespace starvfs {

// The one payload type this library moves around. `.spak` contents
// (proposal §14.2: manifest.cbor, objects/<sector>.cbor, res/ assets, ...)
// are binary, not text, so this is bytes rather than std::string -- a
// std::string invites c_str() and NUL-termination assumptions that binary
// CBOR and compressed zip entries do not satisfy. A caller that wants
// stardata::diag::SourceManager::add_file's std::string does one explicit
// copy at that boundary, which is exactly where the conversion should be
// visible.
using Bytes = std::vector<std::byte>;

} // namespace starvfs
