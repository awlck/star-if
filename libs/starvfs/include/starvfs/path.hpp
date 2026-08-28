// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace starvfs {

// A canonical VFS path: '/'-separated, always relative to the mount root of
// whichever Vfs or Layer it is asked of. There is no absolute form and no
// representation of a path that escapes its root -- see Path::parse.
//
// This is what makes proposal §8.2's sandbox requirement -- "File access is
// exclusively through the VFS API, which is scoped to the game's own mount
// points and cannot escape them" -- hold structurally rather than by
// vigilance. Layer and Vfs take a Path, never a raw string, so there is no
// call site at which an unnormalised name can reach a layer, and no
// traversal check to remember at each of them. Path::parse is the one
// place that check lives; backlog G3's "rejects path traversal in entry
// names" falls out of a ZipLayer using Path for its index rather than
// needing a check of its own.
class Path {
public:
    // Parses and normalises `raw`. Fails (returns nullopt) rather than
    // silently clamping to the root, because a caller that asked for
    // something outside it has a bug worth surfacing, not a path worth
    // guessing at:
    //
    //   - '/' is the only separator. '\' is REJECTED, never translated --
    //     it is a legal byte in a POSIX filename and in a zip entry name,
    //     and translating it would make two distinct entries alias one
    //     another.
    //   - A leading '/' is stripped: every Path is root-relative.
    //   - '.' segments and empty segments ("a//b", a trailing '/') are
    //     dropped.
    //   - '..' pops the preceding segment. Resolving one with nothing to
    //     pop -- ".." at the root, or enough of them to walk past it --
    //     fails the parse instead of clamping to the root.
    //   - An embedded NUL or invalid UTF-8 fails the parse.
    //
    // Windows reserved names (CON, NUL, COM1, ...), and trailing dots or
    // spaces, are deliberately NOT rejected here: a .spak may legitimately
    // contain res/con.png, and the zip layer must be able to serve it.
    // Refusing to write such a name to a real directory is NativeHostIo's
    // problem, not this type's (docs/starvfs-api.md, "Known traps").
    [[nodiscard]] static std::optional<Path> parse(std::string_view raw);

    // The root itself: empty(), str() == "".
    Path() = default;

    [[nodiscard]] std::string_view str() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    // Appends one already-normalised segment (no '/', no "..", no ".", no
    // empty string). For a layer's own path arithmetic -- e.g. building the
    // child paths list() reports for one directory -- not for further
    // caller-supplied input, which should go through parse() again so it
    // gets the same checks.
    [[nodiscard]] Path join(std::string_view segment) const;

    // nullopt at the root; otherwise this path with its final segment
    // removed.
    [[nodiscard]] std::optional<Path> parent() const;

    // The final segment, e.g. "hold.cbor" for "objects/hold.cbor". Empty
    // at the root.
    [[nodiscard]] std::string_view file_name() const noexcept;

    friend bool operator==(const Path& lhs, const Path& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }
    friend bool operator!=(const Path& lhs, const Path& rhs) noexcept { return !(lhs == rhs); }
    friend bool operator<(const Path& lhs, const Path& rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

private:
    explicit Path(std::string value) : value_(std::move(value)) {}

    std::string value_;
};

} // namespace starvfs

template <> struct std::hash<starvfs::Path> {
    std::size_t operator()(const starvfs::Path& path) const noexcept {
        return std::hash<std::string_view>{}(path.str());
    }
};
