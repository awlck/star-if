// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <vector>

#include "stardata/cst/parser.hpp"
#include "stardata/cst/writer.hpp"
#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"

namespace stardata::test {

// Parses a string held in memory, keeping the source, the sink, the cache
// and the tree together. The tree owns its text, so it outlives the source;
// the source is kept anyway because diagnostics reference it by span.
class Parsed {
public:
    explicit Parsed(std::string text, std::string name = "test.star")
        : source_(std::move(text)), id_(sources_.add_file(std::move(name), source_)),
          green_(cst::parse(sources_, id_, cache_, sink_)) {}

    Parsed(const Parsed&) = delete;
    Parsed& operator=(const Parsed&) = delete;

    [[nodiscard]] const diag::SourceManager& sources() const noexcept { return sources_; }
    [[nodiscard]] const diag::DiagnosticSink& sink() const noexcept { return sink_; }
    [[nodiscard]] cst::GreenCache& cache() noexcept { return cache_; }
    [[nodiscard]] const cst::GreenNodePtr& green() const noexcept { return green_; }
    [[nodiscard]] cst::SyntaxNode root() const { return cst::SyntaxNode::root(green_); }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }

    // What the writer produces. For an unmodified parse this must equal
    // source() byte for byte -- the property of backlog E6.
    [[nodiscard]] std::string written() const { return cst::to_text(*green_); }

    [[nodiscard]] std::vector<std::string> codes() const {
        std::vector<std::string> result;
        for (const diag::Diagnostic& diagnostic : sink_.diagnostics()) {
            result.emplace_back(diag::code_string(diagnostic.code()));
        }
        return result;
    }

    [[nodiscard]] bool reported(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink_.diagnostics()) {
            if (diagnostic.code() == code) {
                return true;
            }
        }
        return false;
    }

private:
    std::string source_;
    diag::SourceManager sources_;
    diag::SourceId id_;
    diag::DiagnosticSink sink_;
    cst::GreenCache cache_;
    cst::GreenNodePtr green_;
};

} // namespace stardata::test
