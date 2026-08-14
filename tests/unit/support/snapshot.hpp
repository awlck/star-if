// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <filesystem>
#include <string>

namespace stardata::test {

// True once main() has parsed --update-snapshots off the command line.
[[nodiscard]] bool snapshot_update_requested();

// Backlog C4: diagnostic-producing tests compare against a checked-in
// expected-output file rather than asserting on message text inline, so a
// wording change shows up as a file diff instead of silently changing what
// a REQUIRE happens to accept.
//
// - If the snapshot file does not exist yet, or --update-snapshots was
//   passed, `actual` is (re)written to `snapshot_path` and this returns
//   true.
// - Otherwise, returns whether `actual` matches the stored snapshot
//   byte-for-byte. On a mismatch, both are printed to stderr so a CI log
//   shows the difference without needing local access to the snapshot file.
[[nodiscard]] bool check_snapshot(const std::filesystem::path& snapshot_path,
                                  const std::string& actual);

} // namespace stardata::test
