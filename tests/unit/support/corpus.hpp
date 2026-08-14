// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <algorithm>
#include <filesystem>
#include <vector>

namespace stardata::test {

// Enumerates *.star files directly inside `directory`, sorted for a
// deterministic test order. This is the "corpus-driven test helper that
// discovers .star files rather than listing them" from backlog B3: drop a
// new fixture into tests/corpus/ or tests/corpus/invalid/ and it is picked
// up the next time CTest runs, with no build-file edit.
[[nodiscard]] inline std::vector<std::filesystem::path>
corpus_files(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(directory)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".star") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace stardata::test
