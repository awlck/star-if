// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

namespace stardata::test {

// Reads a file as bytes. Binary, never text mode: the corpus is deliberately
// not line-ending normalised (backlog A4; .gitattributes marks *.star as
// -text), and reading it as text on Windows would quietly turn the CRLF
// fixture into the LF one and make the round-trip test pass for the wrong
// reason.
[[nodiscard]] inline std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// tests/corpus/, wherever the source tree happens to live.
[[nodiscard]] inline std::filesystem::path corpus_dir() {
    return std::filesystem::path(STARIF_CORPUS_DIR);
}

// The name a corpus file is registered under in a SourceManager.
//
// Always repo-relative and always forward-slashed, because diagnostics
// render this path into snapshots that the Windows, macOS and Linux CI legs
// all have to agree on byte for byte. Registering the on-disk path instead
// would bake a build directory into every golden.
[[nodiscard]] inline std::string corpus_name(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(path, std::filesystem::path(STARIF_CORPUS_DIR), ec);
    if (ec || relative.empty()) {
        return path.filename().string();
    }
    return "tests/corpus/" + relative.generic_string();
}

// The codes a fixture declares with `# EXPECT <CODE>` in its header, using
// the same convention as tests/check_stardata.py --self-test.
[[nodiscard]] inline std::set<std::string> expected_codes(const std::string& contents) {
    std::set<std::string> codes;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t hash = line.find_first_not_of(" \t");
        if (hash == std::string::npos) {
            continue;
        }
        if (line[hash] != '#') {
            break; // the header ends at the first non-comment line
        }
        const std::size_t marker = line.find("EXPECT");
        if (marker == std::string::npos) {
            continue;
        }
        std::istringstream rest(line.substr(marker + 6));
        std::string code;
        if (rest >> code) {
            codes.insert(code);
        }
    }
    return codes;
}

} // namespace stardata::test
