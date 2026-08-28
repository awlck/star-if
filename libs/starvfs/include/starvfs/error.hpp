// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace starvfs {

// Nothing in this codebase throws (grep -rn "throw " libs/ is empty), so
// starvfs reports failure as a value, not an exception. A plain enum rather
// than std::error_code: error_code's whole purpose is errno/WinError
// interop, and the entire point of the HostIo boundary (host.hpp) is that
// errno lives on the far side of it. An enum is exhaustively switchable,
// which -Werror turns into a compile error the moment a caller forgets a
// case.
enum class Error {
    NotFound,
    NotAFile,
    NotADirectory,
    ReadOnlyLayer, // decision: docs/starvfs-api.md "Write policy"
    InvalidPath,   // only reachable through a failed Path::parse
    AlreadyExists,
    OutOfRange, // a read_range past end of file
    ArchiveCorrupt,
    UnsupportedCompression,
    HostFailure,    // the frontend's HostIo reported a failure; detail says what
    NotImplemented, // this stub stage (backlog G1) hasn't filled this in yet
};

[[nodiscard]] std::string_view to_string(Error error) noexcept;

// A bare Error is nearly useless in a mod diagnostic -- "NotFound" doesn't
// say which layer looked or what it was looking for -- so every failure
// carries a human-readable detail. `detail` is written by whichever layer
// or host produced the failure and should name itself, the same way
// stardata::schema::SchemaSet::offer's rejections name the declaration's
// owner.
struct Failure {
    Error code;
    std::string detail;
};

[[nodiscard]] inline Failure make_failure(Error code, std::string detail) {
    return Failure{code, std::move(detail)};
}

// A minimal expected<T, Failure>. C++20 has no std::expected; this is the
// ~80-line subset starvfs actually needs, not a general-purpose one.
//
// value()/error() precondition the Result is in the state they ask for --
// std::get<T> throws std::bad_variant_access otherwise. That is a caller
// bug (asking for the success value of a failed read), not a recoverable
// I/O error, so it is a contract violation like std::optional::value() on
// an empty optional, not a case this type reports through Failure.
template <typename T> class Result {
public:
    Result(T value) : storage_(std::move(value)) {}           // NOLINT(*-explicit-*)
    Result(Failure failure) : storage_(std::move(failure)) {} // NOLINT(*-explicit-*)

    [[nodiscard]] bool is_ok() const noexcept { return std::holds_alternative<T>(storage_); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ok(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(storage_); }
    [[nodiscard]] T& value() & { return std::get<T>(storage_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(storage_)); }

    [[nodiscard]] const Failure& error() const& { return std::get<Failure>(storage_); }
    [[nodiscard]] Failure&& error() && { return std::get<Failure>(std::move(storage_)); }

private:
    std::variant<T, Failure> storage_;
};

// Specialised rather than instantiated with T = void (which std::variant
// cannot hold): a write or remove reports only whether it failed.
template <> class Result<void> {
public:
    Result() = default;
    Result(Failure failure) : failure_(std::move(failure)) {} // NOLINT(*-explicit-*)

    [[nodiscard]] bool is_ok() const noexcept { return !failure_.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_ok(); }

    [[nodiscard]] const Failure& error() const& { return *failure_; }
    [[nodiscard]] Failure&& error() && { return std::move(*failure_); }

private:
    std::optional<Failure> failure_;
};

} // namespace starvfs
