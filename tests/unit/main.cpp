// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// A custom entry point instead of Catch2::Catch2WithMain, so this binary
// also accepts --update-snapshots (backlog C4) alongside Catch2's own
// command-line options.

#include <catch2/catch_session.hpp>

#include "support/snapshot.hpp"

namespace {
bool g_update_snapshots = false;
} // namespace

namespace stardata::test {
bool snapshot_update_requested() {
    return g_update_snapshots;
}
} // namespace stardata::test

int main(int argc, char* argv[]) {
    Catch::Session session;

    using Catch::Clara::Opt;
    auto cli = session.cli() | Opt(g_update_snapshots)["--update-snapshots"](
                                   "regenerate diagnostic snapshot files instead of "
                                   "comparing to them");
    session.cli(cli);

    const int return_code = session.applyCommandLine(argc, argv);
    if (return_code != 0) {
        return return_code;
    }
    return session.run();
}
