// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace stardata::diag {

// Opaque handle to a file registered with a SourceManager. Meaningless
// outside the SourceManager that issued it; default-constructed is invalid.
class SourceId {
public:
    constexpr SourceId() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept { return index_ != kInvalid; }

    friend constexpr bool operator==(SourceId lhs, SourceId rhs) noexcept {
        return lhs.index_ == rhs.index_;
    }
    friend constexpr bool operator!=(SourceId lhs, SourceId rhs) noexcept { return !(lhs == rhs); }

private:
    friend class SourceManager;
    static constexpr std::uint32_t kInvalid = static_cast<std::uint32_t>(-1);

    explicit constexpr SourceId(std::uint32_t index) noexcept : index_(index) {}

    std::uint32_t index_ = kInvalid;
};

// A range of bytes in one registered source: [offset, offset + length).
// `length` of 0 denotes a point span, still valid for a caret diagnostic.
struct Span {
    SourceId source;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;

    [[nodiscard]] constexpr std::uint32_t end() const noexcept { return offset + length; }
};

// A 1-based line and column. `column` counts Unicode code points from the
// start of the line, not bytes -- see SourceManager::line_col.
struct LineCol {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

// Registry mapping SourceId to a file's path and contents, and the byte
// offset -> line/column arithmetic every diagnostic span needs.
//
// A SourceManager owns the contents it registers; spans and diagnostics
// reference them only through a SourceId, so nothing above this layer holds
// a dangling string_view once the tree that produced it is done with it.
class SourceManager {
public:
    // Registers a file's full contents and returns a handle to it. Contents
    // are taken as-is: this layer does not normalise line endings, per
    // spec §14.2's byte-exact round-trip requirement.
    SourceId add_file(std::filesystem::path path, std::string contents);

    [[nodiscard]] const std::filesystem::path& path(SourceId id) const;
    [[nodiscard]] std::string_view contents(SourceId id) const;

    // Byte offset -> 1-based line/column. The line table is built lazily on
    // first use for a given source and cached, since most sources are never
    // queried (e.g. a file with no diagnostics).
    //
    // Column counts UTF-8 code points, not bytes, from the start of the
    // line: a tab is one column like any other character (never expanded to
    // a tab stop), and a multi-byte character is one column, not one per
    // byte. `offset` is clamped to the source's length.
    [[nodiscard]] LineCol line_col(SourceId id, std::uint32_t offset) const;

    // The number of code points spanned by [offset, offset + length) within
    // one line -- the width a caret underline needs. Byte length overcounts
    // whenever the span contains a multi-byte character.
    [[nodiscard]] std::uint32_t column_width(SourceId id, std::uint32_t offset,
                                             std::uint32_t length) const;

    // Text of one 1-based line, without its line terminator. Returns an
    // empty view for a line number past the end of the source.
    [[nodiscard]] std::string_view line_text(SourceId id, std::uint32_t line) const;

    [[nodiscard]] std::uint32_t line_count(SourceId id) const;

private:
    struct Entry {
        std::filesystem::path path;
        std::string contents;
        // Byte offset of the first character of each line; line_starts[0]
        // is always 0. Empty until the first line_col/line_text call.
        mutable std::vector<std::uint32_t> line_starts;
    };

    [[nodiscard]] const Entry& entry(SourceId id) const;
    [[nodiscard]] static const std::vector<std::uint32_t>& line_starts(const Entry& entry);

    std::vector<Entry> entries_;
};

} // namespace stardata::diag
