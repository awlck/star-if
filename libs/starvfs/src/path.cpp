// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "starvfs/path.hpp"

#include <vector>

namespace starvfs {

namespace {

// A minimal UTF-8 validator. starvfs stays independent of stardata
// (workstream G's own preamble: "Independent of stardata -- good parallel
// work"), so this does not reach for stardata's lexer; it needs to reject
// malformed bytes, not decode code points, so it is a dozen lines rather
// than a dependency.
[[nodiscard]] bool is_valid_utf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t continuation_bytes = 0;
        if (byte < 0x80) {
            continuation_bytes = 0;
        } else if ((byte & 0xE0) == 0xC0) {
            continuation_bytes = 1;
        } else if ((byte & 0xF0) == 0xE0) {
            continuation_bytes = 2;
        } else if ((byte & 0xF8) == 0xF0) {
            continuation_bytes = 3;
        } else {
            return false; // stray continuation byte or an invalid leading byte
        }
        if (i + continuation_bytes >= text.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= continuation_bytes; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += continuation_bytes + 1;
    }
    return true;
}

} // namespace

std::optional<Path> Path::parse(std::string_view raw) {
    if (raw.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    if (raw.find('\\') != std::string_view::npos) {
        return std::nullopt;
    }
    if (!is_valid_utf8(raw)) {
        return std::nullopt;
    }

    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t slash = raw.find('/', start);
        const std::string_view segment = raw.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (segment == "." || segment.empty()) {
            // dropped: "." and empty segments (leading '/', "a//b", a
            // trailing '/') carry no information.
        } else if (segment == "..") {
            if (segments.empty()) {
                return std::nullopt; // would pop above the root
            }
            segments.pop_back();
        } else {
            segments.push_back(segment);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    std::string normalised;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) {
            normalised.push_back('/');
        }
        normalised.append(segments[i]);
    }
    return Path(std::move(normalised));
}

Path Path::join(std::string_view segment) const {
    if (segment.empty()) {
        return *this;
    }
    if (value_.empty()) {
        return Path(std::string(segment));
    }
    std::string joined = value_;
    joined.push_back('/');
    joined.append(segment);
    return Path(std::move(joined));
}

std::optional<Path> Path::parent() const {
    if (value_.empty()) {
        return std::nullopt;
    }
    const std::size_t slash = value_.rfind('/');
    if (slash == std::string::npos) {
        return Path(); // one segment deep; the parent is the root
    }
    return Path(value_.substr(0, slash));
}

std::string_view Path::file_name() const noexcept {
    const std::size_t slash = value_.rfind('/');
    if (slash == std::string::npos) {
        return value_;
    }
    return std::string_view(value_).substr(slash + 1);
}

} // namespace starvfs
