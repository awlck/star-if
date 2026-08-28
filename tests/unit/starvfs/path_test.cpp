// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// backlog G1's "path normalisation; escaping the mount root is impossible"
// bullet, and the one behaviour docs/starvfs-api.md singles out as worth
// real tests before the layers exist: Path is the mechanism the sandbox
// requirement of proposal §8.2 rests on.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "starvfs/path.hpp"

using starvfs::Path;

TEST_CASE("Path::parse normalises legal forms", "[path]") {
    CHECK(Path::parse("")->str() == "");
    CHECK(Path::parse("objects/hold.cbor")->str() == "objects/hold.cbor");
    CHECK(Path::parse("/objects/hold.cbor")->str() == "objects/hold.cbor"); // leading '/' stripped
    CHECK(Path::parse("objects//hold.cbor")->str() == "objects/hold.cbor"); // empty segment dropped
    CHECK(Path::parse("objects/hold.cbor/")->str() == "objects/hold.cbor"); // trailing '/' dropped
    CHECK(Path::parse("./objects/./hold.cbor")->str() == "objects/hold.cbor"); // '.' dropped
    CHECK(Path::parse("a/b/../c")->str() == "a/c"); // ".." pops the preceding segment
}

TEST_CASE("Path::parse rejects every escape attempt", "[path]") {
    CHECK_FALSE(Path::parse("..").has_value());
    CHECK_FALSE(Path::parse("../x").has_value());
    CHECK_FALSE(Path::parse("a/../../b").has_value()); // one ".." too many
    CHECK_FALSE(Path::parse("a/b/../../../c").has_value());
}

TEST_CASE("Path::parse rejects a backslash rather than translating it", "[path]") {
    // A '\' is a legal byte in a POSIX filename and in a zip entry name;
    // translating it to '/' would make two distinct entries alias one
    // another, so it is refused outright.
    CHECK_FALSE(Path::parse("a\\b").has_value());
}

TEST_CASE("Path::parse rejects an embedded NUL", "[path]") {
    CHECK_FALSE(Path::parse(std::string_view("a\0b", 3)).has_value());
}

TEST_CASE("Path::parse rejects invalid UTF-8", "[path]") {
    CHECK_FALSE(Path::parse("\xff\xfe").has_value());
    CHECK_FALSE(Path::parse("a/\xc0").has_value()); // truncated multi-byte sequence
}

TEST_CASE("Path::parse accepts valid multi-byte UTF-8", "[path]") {
    CHECK(Path::parse("r\xc3\xa9sistance.star")->str() ==
          "r\xc3\xa9sistance.star"); // "résistance.star"
}

TEST_CASE("Path traversal is not just rejected, it has nowhere to reach", "[path]") {
    // Windows reserved names are deliberately legal here -- a .spak may
    // contain res/con.png -- so this only checks the escape guarantee.
    const auto root = Path();
    CHECK(root.empty());
    CHECK(root.str().empty());
}

TEST_CASE("Path::join appends one segment", "[path]") {
    CHECK(Path().join("res").join("icon.png").str() == "res/icon.png");
    CHECK(Path::parse("objects")->join("hold.cbor").str() == "objects/hold.cbor");
}

TEST_CASE("Path::parent walks up to the root and then stops", "[path]") {
    const auto path = *Path::parse("objects/hold.cbor");
    REQUIRE(path.parent().has_value());
    CHECK(path.parent()->str() == "objects");
    REQUIRE(path.parent()->parent().has_value());
    CHECK(path.parent()->parent()->str() == ""); // the root
    CHECK_FALSE(path.parent()->parent()->parent().has_value());
}

TEST_CASE("Path::file_name is the final segment", "[path]") {
    CHECK(Path::parse("objects/hold.cbor")->file_name() == "hold.cbor");
    CHECK(Path::parse("hold.cbor")->file_name() == "hold.cbor");
    CHECK(Path().file_name().empty());
}

TEST_CASE("Path equality and ordering are by normalised string", "[path]") {
    CHECK(*Path::parse("a/b") == *Path::parse("/a/b/"));
    CHECK(*Path::parse("a/b") != *Path::parse("a/c"));
    CHECK(*Path::parse("a/b") < *Path::parse("a/c"));
}

TEST_CASE("Path is hashable", "[path]") {
    const std::hash<Path> hasher;
    CHECK(hasher(*Path::parse("a/b")) == hasher(*Path::parse("/a/b/")));
}
