// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "stardata/cst/green.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/schema/loader.hpp"

namespace stardata::test {

[[nodiscard]] inline std::filesystem::path builtin_dir() {
    return std::filesystem::path(STARIF_BUILTIN_DIR);
}

[[nodiscard]] inline std::filesystem::path stdlib_dir() {
    return std::filesystem::path(STARIF_STDLIB_DIR) / "stdlib";
}

// A load, with everything it needs kept alive together. Diagnostics hold
// spans into the SourceManager, so the two have to travel as one -- a test
// that let the manager die would render its own failures as garbage.
//
// Built in place rather than returned from a factory: GreenCache guards its
// interning table with a mutex and so cannot be moved, which is the right
// trade for the cache and merely an inconvenience here.
class LoadedSet {
public:
    LoadedSet() = default;
    LoadedSet(const LoadedSet&) = delete;
    LoadedSet& operator=(const LoadedSet&) = delete;

    // libs/starcore/builtin/, as `starcore` owns it.
    void load_builtin() { load_from(builtin_dir(), "starcore", /*is_core=*/true); }

    // stdlib/stdlib/, which is ordinary Stardata with no privileged status.
    // Owned by `stdlib`, its own `library` id: an owner is the name
    // `@replaces` uses (§7.6), so it has to be the library id and not a
    // path. The built-in set's owner is `starcore`, which is not a library
    // and cannot be named by `@replaces` for exactly that reason.
    void load_stdlib() { load_from(stdlib_dir(), "stdlib"); }

    // One string loaded as if it were a library file. This is how the
    // sealing assertions of backlog F2a are exercised: a library trying to
    // do something only core may do.
    void load_text(std::string text, std::string owner = "a library",
                   std::string name = "library.star", bool is_core = false) {
        const diag::SourceId id = sources.add_file(std::move(name), std::move(text));
        schema::load_source(id, schema::LoadOptions{std::move(owner), is_core}, sources, cache, set,
                            sink);
    }

    [[nodiscard]] bool reported(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (diagnostic.code() == code) {
                return true;
            }
        }
        return false;
    }

    // The first diagnostic of a given code, for a test that wants to read
    // the message rather than merely count it.
    [[nodiscard]] const diag::Diagnostic* first(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink.diagnostics()) {
            if (diagnostic.code() == code) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

    diag::SourceManager sources;
    diag::DiagnosticSink sink;
    cst::GreenCache cache;
    schema::SchemaSet set;
    std::vector<std::filesystem::path> files;

private:
    void load_from(const std::filesystem::path& directory, std::string owner,
                   bool is_core = false) {
        const std::vector<std::filesystem::path> loaded = schema::load_directory(
            directory, schema::LoadOptions{std::move(owner), is_core}, sources, cache, set, sink);
        files.insert(files.end(), loaded.begin(), loaded.end());
    }
};

} // namespace stardata::test
