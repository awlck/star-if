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

// Which pass owns which diagnostic code.
//
// Shared, because two tests need the same answer for opposite reasons: each
// owning pass's corpus test asserts its codes fire, and the front end's
// snapshot says which pass owns a code it does not itself produce. Without
// that, a snapshot reading "(none)" cannot be told apart from one nothing
// checks at all -- which is exactly the question a reader asks of it first.
//
// There are two sets rather than one because there are two libraries. The
// mechanism/vocabulary line of proposal §2.1.1 runs through the diagnostics
// as much as through the code: E-UNKNOWN-KEY is a fact about a schema, and
// E-PLACEMENT-CONFLICT is a fact about containment.

// `libs/starcore`'s: the vocabulary of spec §8-§12.
//
// The four text-layer codes are here rather than with the schema layer's
// because §9.3's `style` form and §9.6's `loc` form are what give them
// meaning, and both are core-owned by §7.2.4's test -- starcore/text.cpp is
// the code that reads them. E-TEMPLATE-BRACKETS is the exception and sits
// below: which values are templates is a question about declared types
// (§6.2), which is the schema layer's, and that is where it is reported.
[[nodiscard]] inline const std::set<std::string>& starcore_codes() {
    static const std::set<std::string> codes = {
        "E-PLACEMENT-CONFLICT",  "E-PROP-ABSENT",     "E-PROP-MAYBE-ABSENT", "E-STYLE-UNDECLARED",
        "E-LOC-DUPLICATE",       "E-LOC-UNDEFINED",   "W-LOC-UNUSED",        "E-FAILMSG-SILENT",
        "E-FAILMSG-UNREACHABLE", "W-FAILMSG-MISSING", "E-FLAG-UNDECLARED",   "E-FLAG-NOT-BOOL",
        "E-GLOBAL-UNDECLARED",   "W-GLOBAL-UNUSED"};
    return codes;
}

// `libs/stardata`'s schema layer: the mechanism of spec §5-§7.
[[nodiscard]] inline const std::set<std::string>& schema_layer_codes() {
    static const std::set<std::string> codes = {
        "E-SCHEMA-INVALID",        "E-SCHEMA-DUPLICATE",  "E-SCHEMA-SEALED",
        "E-KEY-MISSING",           "E-CORE-REPARENT",     "E-CORE-REQUIREMENT",
        "E-PROPDEF-TYPE-MISMATCH", "E-UNKNOWN-KEY",       "W-PROVIDES-MISMATCH",
        "E-CORE-RESERVED",         "W-PROPDEF-REDUNDANT", "E-DUP-KEY",
        "E-EXCLUSIVE-GROUP",       "E-EXCLUSIVE-MISSING", "E-TYPE-MISMATCH",
        "E-UNKNOWN-ANNOTATION",    "E-ANNOT-CONFLICT",    "E-ANNOT-MISAPPLIED",
        "E-ANNOT-ARGUMENT",        "E-TEMPLATE-BRACKETS", "E-REF-UNRESOLVED"};
    return codes;
}

// Whether a fixture asks to be loaded as `starcore`'s own, with
// `# LOAD-AS core` in its header. The default is a library, which is what
// nearly every negative fixture wants to be: the situation being tested is
// usually somebody overstepping.
//
// The exception is a fixture about core getting its own house wrong -- a
// requirement nothing satisfies, say -- which only means anything when core
// is the one saying it.
[[nodiscard]] inline bool loads_as_core(const std::string& contents) {
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        if (line[start] != '#') {
            break; // the header ends at the first non-comment line
        }
        if (line.find("LOAD-AS core") != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The codes a file suppresses for its whole length with
// `# check: allow W-LOC-UNUSED, ...`, the pragma tests/check_stardata.py
// documents under SUPPRESSION.
//
// Read here so that the two checkers agree about a file rather than only
// about a diagnostic. tour.star carries one: its §18 declares loc entries
// purely to demonstrate string syntax and its §17 declares the engine's own
// fallback message, and neither is a real unused string. A C++ pass that
// ignored the pragma would report eight warnings the Python one is told to
// skip, and the corpus would be "clean" under one checker and not the other.
[[nodiscard]] inline std::set<std::string> allowed_codes(const std::string& contents) {
    std::set<std::string> codes;
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t marker = line.find("check: allow ");
        const std::size_t hash = line.find_first_not_of(" \t");
        if (hash == std::string::npos || line[hash] != '#' || marker == std::string::npos) {
            continue;
        }
        std::istringstream rest(line.substr(marker + 13));
        std::string code;
        while (rest >> code) {
            if (!code.empty() && code.back() == ',') {
                code.pop_back();
            }
            codes.insert(code);
        }
    }
    return codes;
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
