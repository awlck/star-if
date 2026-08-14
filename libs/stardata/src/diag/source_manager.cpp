// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/diag/source_manager.hpp"

#include <algorithm>
#include <cstddef>

namespace stardata::diag {

namespace {

// A UTF-8 continuation byte (10xxxxxx) is not the start of a code point, so
// counting columns means counting bytes that are NOT continuation bytes.
[[nodiscard]] bool is_utf8_lead_byte(unsigned char byte) noexcept {
    return (byte & 0xC0) != 0x80;
}

[[nodiscard]] std::uint32_t count_code_points(std::string_view text) noexcept {
    std::uint32_t count = 0;
    for (unsigned char byte : text) {
        if (is_utf8_lead_byte(byte)) {
            ++count;
        }
    }
    return count;
}

} // namespace

SourceId SourceManager::add_file(std::filesystem::path path, std::string contents) {
    entries_.push_back(Entry{std::move(path), std::move(contents), {}});
    return SourceId(static_cast<std::uint32_t>(entries_.size() - 1));
}

const SourceManager::Entry& SourceManager::entry(SourceId id) const {
    return entries_.at(id.index_);
}

const std::filesystem::path& SourceManager::path(SourceId id) const {
    return entry(id).path;
}

std::string_view SourceManager::contents(SourceId id) const {
    return entry(id).contents;
}

const std::vector<std::uint32_t>& SourceManager::line_starts(const Entry& ent) {
    if (ent.line_starts.empty()) {
        ent.line_starts.push_back(0);
        for (std::size_t i = 0; i < ent.contents.size(); ++i) {
            if (ent.contents[i] == '\n') {
                ent.line_starts.push_back(static_cast<std::uint32_t>(i + 1));
            }
        }
    }
    return ent.line_starts;
}

LineCol SourceManager::line_col(SourceId id, std::uint32_t offset) const {
    const Entry& ent = entry(id);
    offset = std::min(offset, static_cast<std::uint32_t>(ent.contents.size()));
    const auto& starts = line_starts(ent);

    // Index of the last line start <= offset.
    auto it = std::upper_bound(starts.begin(), starts.end(), offset);
    const std::size_t line_index = static_cast<std::size_t>(it - starts.begin()) - 1;

    LineCol result;
    result.line = static_cast<std::uint32_t>(line_index + 1);
    std::string_view prefix(ent.contents.data() + starts[line_index], offset - starts[line_index]);
    result.column = count_code_points(prefix) + 1;
    return result;
}

std::uint32_t SourceManager::column_width(SourceId id, std::uint32_t offset,
                                          std::uint32_t length) const {
    const Entry& ent = entry(id);
    const auto size = static_cast<std::uint32_t>(ent.contents.size());
    offset = std::min(offset, size);
    const std::uint32_t end = std::min(offset + length, size);
    if (end <= offset) {
        return 0;
    }
    return count_code_points(std::string_view(ent.contents.data() + offset, end - offset));
}

std::string_view SourceManager::line_text(SourceId id, std::uint32_t line) const {
    const Entry& ent = entry(id);
    const auto& starts = line_starts(ent);
    if (line == 0 || line > starts.size()) {
        return {};
    }
    const std::uint32_t start = starts[line - 1];
    const std::uint32_t stop = (line < starts.size())
                                   ? starts[line] - 1 // exclude the '\n'
                                   : static_cast<std::uint32_t>(ent.contents.size());
    std::uint32_t end = stop;
    // A CRLF fixture's line body still ends in '\r'; trim it for display,
    // matching the tests/corpus crlf.star fixture's line endings without
    // reproducing them in a rendered diagnostic.
    if (end > start && ent.contents[end - 1] == '\r') {
        --end;
    }
    return {ent.contents.data() + start, end - start};
}

std::uint32_t SourceManager::line_count(SourceId id) const {
    return static_cast<std::uint32_t>(line_starts(entry(id)).size());
}

} // namespace stardata::diag
