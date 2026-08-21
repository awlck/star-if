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

[[nodiscard]] inline std::filesystem::path source_dir() {
    return std::filesystem::path(STARIF_SOURCE_DIR);
}

[[nodiscard]] inline std::filesystem::path format_dir() {
    return std::filesystem::path(STARIF_FORMAT_DIR);
}

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

    // libs/stardata/builtin/, the forms the format layer parses itself
    // (§7.2.4). Owned by `stardata`, sealed, and first: `class` has to be a
    // known form before any file declaring a class loads, and every other
    // builtin here declares one.
    //
    // `stardata` is not a library id, so no `@replaces(stardata)` can claim
    // these -- the same protection `starcore` has, for the same reason.
    void load_format() { load_from(format_dir(), "stardata", /*is_core=*/true); }

    // libs/starcore/builtin/, as `starcore` owns it. Loads the format set
    // first, since every caller wants both and forgetting one is silent.
    //
    // No longer tells the set anything about placement. It used to name the
    // relation enum here, standing in for what `starcore` would do in Phase 1;
    // `libs/starcore` now names it itself, which is the whole of proposal
    // §2.1.1's point -- the stand-in existed only because the pass was on the
    // wrong side of the line.
    void load_builtin() {
        load_format();
        load_from(builtin_dir(), "starcore", /*is_core=*/true);
    }

    // stdlib/stdlib/, which is ordinary Stardata with no privileged status.
    // Owned by `stdlib`, its own `library` id: an owner is the name
    // `@replaces` uses (§7.6), so it has to be the library id and not a
    // path. The built-in set's owner is `starcore`, which is not a library
    // and cannot be named by `@replaces` for exactly that reason.
    void load_stdlib() { load_from(stdlib_dir(), "stdlib"); }

    // One string loaded as if it were a library file. This is how the
    // sealing assertions of backlog F2a are exercised: a library trying to
    // do something only core may do.
    //
    // Returns the id, so that a caller wanting the tree as well as the
    // registry can re-view the same source rather than adding a second copy
    // of the text to a second manager. Two copies means two sets of spans
    // that render only against the manager they came from, which is a
    // confusing way for a test to fail.
    diag::SourceId load_text(std::string text, std::string owner = "a library",
                             std::string name = "library.star", bool is_core = false) {
        const diag::SourceId id = sources.add_file(std::move(name), std::move(text));
        schema::load_source(id, schema::LoadOptions{std::move(owner), is_core, {}}, sources, cache,
                            set, sink);
        return id;
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
        // Names recorded relative to the repository root, so a diagnostic
        // citing a built-in file reads the same in every checkout -- and a
        // golden that captures one does not bake in the machine that wrote
        // it. `source_dir()` is CMAKE_SOURCE_DIR.
        const schema::LoadOptions options{std::move(owner), is_core, source_dir()};
        const std::vector<std::filesystem::path> loaded =
            schema::load_directory(directory, options, sources, cache, set, sink);
        files.insert(files.end(), loaded.begin(), loaded.end());
    }
};

} // namespace stardata::test
