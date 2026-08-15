// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "stardata/diag/codes.hpp"
#include "stardata/diag/sink.hpp"
#include "stardata/diag/source_manager.hpp"
#include "stardata/lex/lexer.hpp"

namespace stardata::test {

// Lexes a string held in memory, keeping the SourceManager, the sink and the
// stream together so a test can ask about any of them. Everything the lexer
// touches is span-based, so the source has to outlive the stream -- bundling
// them is what makes that automatic.
class Lexed {
public:
    explicit Lexed(std::string text, std::string name = "test.star")
        : id_(sources_.add_file(std::move(name), std::move(text))),
          stream_(lex::lex(sources_, id_, sink_)) {}

    Lexed(const Lexed&) = delete;
    Lexed& operator=(const Lexed&) = delete;

    [[nodiscard]] const diag::SourceManager& sources() const noexcept { return sources_; }
    [[nodiscard]] diag::SourceId id() const noexcept { return id_; }
    [[nodiscard]] const diag::DiagnosticSink& sink() const noexcept { return sink_; }
    [[nodiscard]] const lex::TokenStream& stream() const noexcept { return stream_; }

    // The token kinds in order, with the trailing EndOfFile dropped: a test
    // asserting on the shape of a stream should not have to spell it out
    // every time.
    [[nodiscard]] std::vector<lex::TokenKind> kinds() const {
        std::vector<lex::TokenKind> result;
        for (const lex::Token& token : stream_.tokens()) {
            if (token.kind != lex::TokenKind::EndOfFile) {
                result.push_back(token.kind);
            }
        }
        return result;
    }

    [[nodiscard]] std::string_view text_of(std::size_t index) const {
        return sources_.text(stream_[index].span);
    }

    // The diagnostic codes reported, in order, as their stable strings --
    // what the `# EXPECT` fixtures and CI assertions key on.
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

    // The first diagnostic carrying `code`, for tests that assert on its
    // span, notes or fix-its. Null when it was never reported.
    [[nodiscard]] const diag::Diagnostic* find(diag::Code code) const {
        for (const diag::Diagnostic& diagnostic : sink_.diagnostics()) {
            if (diagnostic.code() == code) {
                return &diagnostic;
            }
        }
        return nullptr;
    }

private:
    diag::SourceManager sources_;
    diag::SourceId id_;
    diag::DiagnosticSink sink_;
    lex::TokenStream stream_;
};

} // namespace stardata::test
