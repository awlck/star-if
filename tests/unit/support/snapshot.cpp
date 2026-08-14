// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "support/snapshot.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace stardata::test {

namespace {

std::string read_file(const std::filesystem::path& path, bool& existed) {
    std::ifstream in(path, std::ios::binary);
    existed = in.good();
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

} // namespace

bool check_snapshot(const std::filesystem::path& snapshot_path, const std::string& actual) {
    bool existed = false;
    const std::string expected = read_file(snapshot_path, existed);

    if (!existed || snapshot_update_requested()) {
        write_file(snapshot_path, actual);
        std::cerr << "snapshot: " << (existed ? "updated " : "wrote new file ")
                  << snapshot_path.string() << '\n';
        return true;
    }

    if (actual == expected) {
        return true;
    }

    std::cerr << "snapshot mismatch: " << snapshot_path.string() << "\n--- expected ---\n"
              << expected << "\n--- actual ---\n"
              << actual << "\n--- re-run with --update-snapshots to accept ---\n";
    return false;
}

} // namespace stardata::test
