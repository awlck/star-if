// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/schema/suggest.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace stardata::schema {

namespace {

[[nodiscard]] char lower(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

[[nodiscard]] bool same_but_for_case(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char x, char y) { return lower(x) == lower(y); });
}

} // namespace

std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t limit) noexcept {
    const std::size_t over = limit + 1;

    // A length difference is a lower bound on the distance, so this rejects
    // the great majority of candidates without allocating anything.
    if (a.size() > b.size() + limit || b.size() > a.size() + limit) {
        return over;
    }
    if (a.empty()) {
        return b.size() > limit ? over : b.size();
    }
    if (b.empty()) {
        return a.size() > limit ? over : a.size();
    }

    // Three rows rather than the full matrix: Damerau's transposition case
    // reaches back two rows and no further.
    std::vector<std::size_t> before_previous(b.size() + 1);
    std::vector<std::size_t> previous(b.size() + 1);
    std::vector<std::size_t> current(b.size() + 1);

    for (std::size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        std::size_t best_in_row = current[0];

        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            std::size_t at = std::min({
                previous[j] + 1,        // deletion
                current[j - 1] + 1,     // insertion
                previous[j - 1] + cost, // substitution
            });
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                at = std::min(at, before_previous[j - 2] + 1); // transposition
            }
            current[j] = at;
            best_in_row = std::min(best_in_row, at);
        }

        // Every later row is at least as large as this row's minimum, so once
        // the whole row is past the limit the answer is too.
        if (best_in_row > limit) {
            return over;
        }

        before_previous.swap(previous);
        previous.swap(current);
    }

    return previous[b.size()] > limit ? over : previous[b.size()];
}

std::size_t distance_limit(std::string_view written, std::string_view candidate) noexcept {
    return std::max<std::size_t>(1, std::max(written.size(), candidate.size()) / 3);
}

std::optional<std::string_view> nearest(std::string_view written,
                                        const std::vector<std::string_view>& candidates) {
    if (written.empty()) {
        return std::nullopt;
    }

    std::optional<std::string_view> best;
    std::size_t best_distance = 0;

    for (const std::string_view candidate : candidates) {
        if (candidate.empty() || candidate == written) {
            continue; // an exact match is not a suggestion; the caller has other news
        }
        if (same_but_for_case(candidate, written)) {
            return candidate; // outright, whatever any distance says
        }

        const std::size_t limit = distance_limit(written, candidate);
        const std::size_t distance = edit_distance(written, candidate, limit);
        if (distance > limit) {
            continue;
        }
        // Strictly nearer, so the FIRST of equally-near candidates wins and
        // the answer follows declaration order rather than iteration order.
        if (!best || distance < best_distance) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

bool suggest(diag::Diagnostic& diagnostic, diag::Span at, std::string_view written,
             const std::vector<std::string_view>& candidates) {
    const std::optional<std::string_view> guess = nearest(written, candidates);
    if (!guess) {
        return false;
    }
    diagnostic.with_fix_it(at, std::string(*guess), "did you mean '" + std::string(*guess) + "'?");
    return true;
}

} // namespace stardata::schema
