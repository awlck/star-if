// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "starvfs/error.hpp"

namespace starvfs {

// proposal §12.5: "the VFS API must be async-capable from day one, even
// though the desktop implementation is synchronous. Retrofitting this
// later is expensive." This is that requirement, met without threads:
//
//   - Future<T>::ready()/failed() build an ALREADY-COMPLETE future with no
//     allocation and no shared state -- a resident layer's read() is a
//     plain return, exactly as cheap as returning T would have been.
//   - A pending future holds refcounted state a Promise<T> completes later.
//     Nothing here spawns a thread or blocks: on the desktop nothing is
//     ever actually pending; on the web target (proposal §12.5's
//     Emscripten/WASM build, single-threaded) a HostIo implementation
//     completes its promises from the same event-loop turn that receives
//     the browser's I/O callback, and Vfs::pump()/HostIo::pump() is what
//     lets a frontend drive that without starvfs owning an event loop of
//     its own.
//   - then()'s continuation runs INLINE, on the caller's thread, the moment
//     the future is (or becomes) ready. Code written against then() is
//     therefore correct on the synchronous desktop path with no pump call
//     at all, and unchanged when a real async host is plugged in.
//
// Cancellation is deliberately absent. Nothing in the engine's read
// pattern has a cancel story yet -- a token nobody honours would be worse
// than none -- so this is a known gap, recorded here rather than
// half-built.
//
// A C++20 coroutine `co_await` adaptor (await_ready/await_suspend/
// await_resume) is a non-breaking addition to this class once a scheduler
// exists to drive it. Recorded here so a future change adds the adaptor
// rather than replacing the design: coroutines were considered for this
// task and set aside because they are viral (every caller up to the
// sector loader would become a coroutine) and need a scheduler this
// project does not have yet.
template <typename T> class Promise;

namespace detail {

template <typename T> struct FutureState {
    std::optional<Result<T>> result;
    std::function<void(Result<T>)> continuation;
};

} // namespace detail

template <typename T> class Future {
public:
    using Continuation = std::function<void(Result<T>)>;

    [[nodiscard]] static Future ready(T value) { return Future(Result<T>(std::move(value))); }
    [[nodiscard]] static Future failed(Failure failure) {
        return Future(Result<T>(std::move(failure)));
    }

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    [[nodiscard]] bool is_ready() const noexcept {
        if (std::holds_alternative<Result<T>>(storage_)) {
            return true;
        }
        return std::get<std::shared_ptr<detail::FutureState<T>>>(storage_)->result.has_value();
    }

    // Consumes the future. Precondition: is_ready(). Calling this on a
    // pending future is a caller bug (there is nothing to take yet), not a
    // Failure this type reports -- same contract as Result::value().
    [[nodiscard]] Result<T> take() {
        if (auto* ready = std::get_if<Result<T>>(&storage_)) {
            return std::move(*ready);
        }
        auto& state = *std::get<std::shared_ptr<detail::FutureState<T>>>(storage_);
        return std::move(*state.result);
    }

    // Runs `continuation` with the result once this future completes -- or
    // immediately, before then() returns, if it already has. Consumes the
    // future: a Future is a one-shot handle to one result, not a stream.
    void then(Continuation continuation) && {
        if (auto* ready = std::get_if<Result<T>>(&storage_)) {
            continuation(std::move(*ready));
            return;
        }
        auto state = std::get<std::shared_ptr<detail::FutureState<T>>>(std::move(storage_));
        if (state->result.has_value()) {
            continuation(std::move(*state->result));
        } else {
            state->continuation = std::move(continuation);
        }
    }

private:
    friend class Promise<T>;

    explicit Future(Result<T> value) : storage_(std::move(value)) {}
    explicit Future(std::shared_ptr<detail::FutureState<T>> state) : storage_(std::move(state)) {}

    std::variant<Result<T>, std::shared_ptr<detail::FutureState<T>>> storage_;
};

// The write side of a pending Future<T>. A HostIo or Layer implementation
// that cannot answer synchronously creates one, hands out its future() to
// the caller, and resolve()s or reject()s it once the real I/O completes
// (on the same thread -- see the note above future.hpp's Future).
template <typename T> class Promise {
public:
    Promise() : state_(std::make_shared<detail::FutureState<T>>()) {}

    // Non-consuming and callable more than once is deliberately not
    // supported: exactly one Future is handed out per Promise, matching
    // the one-shot contract Future::then() documents.
    [[nodiscard]] Future<T> future() const { return Future<T>(state_); }

    void resolve(T value) { complete(Result<T>(std::move(value))); }
    void reject(Failure failure) { complete(Result<T>(std::move(failure))); }

private:
    void complete(Result<T> result) {
        state_->result = std::move(result);
        if (state_->continuation) {
            auto continuation = std::move(state_->continuation);
            continuation(*state_->result);
        }
    }

    std::shared_ptr<detail::FutureState<T>> state_;
};

// Explicit specialisations for T = void.
//
// `Future<T>::ready(T value)` and `Promise<T>::resolve(T value)` cannot be
// instantiated at T = void -- a parameter of type "void value" is
// ill-formed -- which is the same reason the standard library gives
// std::promise<void> its own specialisation rather than a generic one. This
// mirrors the primary template exactly except that ready()/resolve() take
// no argument; every other member has the same behaviour and the same
// comments above apply.
template <> class Future<void> {
public:
    using Continuation = std::function<void(Result<void>)>;

    [[nodiscard]] static Future ready() { return Future(Result<void>()); }
    [[nodiscard]] static Future failed(Failure failure) {
        return Future(Result<void>(std::move(failure)));
    }

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    [[nodiscard]] bool is_ready() const noexcept {
        if (std::holds_alternative<Result<void>>(storage_)) {
            return true;
        }
        return std::get<std::shared_ptr<detail::FutureState<void>>>(storage_)->result.has_value();
    }

    [[nodiscard]] Result<void> take() {
        if (auto* ready = std::get_if<Result<void>>(&storage_)) {
            return std::move(*ready);
        }
        auto& state = *std::get<std::shared_ptr<detail::FutureState<void>>>(storage_);
        return std::move(*state.result);
    }

    void then(Continuation continuation) && {
        if (auto* ready = std::get_if<Result<void>>(&storage_)) {
            continuation(std::move(*ready));
            return;
        }
        auto state = std::get<std::shared_ptr<detail::FutureState<void>>>(std::move(storage_));
        if (state->result.has_value()) {
            continuation(std::move(*state->result));
        } else {
            state->continuation = std::move(continuation);
        }
    }

private:
    friend class Promise<void>;

    explicit Future(Result<void> value) : storage_(std::move(value)) {}
    explicit Future(std::shared_ptr<detail::FutureState<void>> state)
        : storage_(std::move(state)) {}

    std::variant<Result<void>, std::shared_ptr<detail::FutureState<void>>> storage_;
};

template <> class Promise<void> {
public:
    Promise() : state_(std::make_shared<detail::FutureState<void>>()) {}

    [[nodiscard]] Future<void> future() const { return Future<void>(state_); }

    void resolve() { complete(Result<void>()); }
    void reject(Failure failure) { complete(Result<void>(std::move(failure))); }

private:
    void complete(Result<void> result) {
        state_->result = std::move(result);
        if (state_->continuation) {
            auto continuation = std::move(state_->continuation);
            continuation(*state_->result);
        }
    }

    std::shared_ptr<detail::FutureState<void>> state_;
};

} // namespace starvfs
