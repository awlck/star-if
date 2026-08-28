// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
//
// backlog G1: "read(path) -> future<bytes> with a synchronous fast path
// for resident layers" -- the async shape proposal §12.5 asks be settled
// from day one. These tests cover Future/Promise's own contract, not any
// layer built on top of it (the layers are still stubs; see
// docs/starvfs-api.md).
#include <catch2/catch_test_macros.hpp>

#include "starvfs/future.hpp"

using starvfs::Error;
using starvfs::Future;
using starvfs::make_failure;
using starvfs::Promise;
using starvfs::Result;

TEST_CASE("a ready future reports is_ready with no promise involved", "[future]") {
    auto future = Future<int>::ready(42);
    CHECK(future.is_ready());
}

TEST_CASE("then on an already-ready future runs the continuation inline", "[future]") {
    // The property that makes desktop code correct with no pump() call at
    // all: the continuation must have already run by the time then()
    // returns, not merely be scheduled to.
    auto future = Future<int>::ready(42);
    bool ran = false;
    std::move(future).then([&](Result<int> result) {
        ran = true;
        REQUIRE(result.is_ok());
        CHECK(result.value() == 42);
    });
    CHECK(ran);
}

TEST_CASE("a failed future carries its Failure through then", "[future]") {
    auto future = Future<int>::failed(make_failure(Error::NotFound, "objects/hold.cbor"));
    std::move(future).then([](Result<int> result) {
        REQUIRE_FALSE(result.is_ok());
        CHECK(result.error().code == Error::NotFound);
        CHECK(result.error().detail == "objects/hold.cbor");
    });
}

TEST_CASE("a pending future is not ready until its promise resolves", "[future]") {
    Promise<int> promise;
    Future<int> future = promise.future();
    CHECK_FALSE(future.is_ready());

    bool ran = false;
    std::move(future).then([&](Result<int> result) {
        ran = true;
        REQUIRE(result.is_ok());
        CHECK(result.value() == 7);
    });
    CHECK_FALSE(ran); // then() attached before resolve(): must not fire early

    promise.resolve(7);
    CHECK(ran);
}

TEST_CASE("a pending future can be rejected instead of resolved", "[future]") {
    Promise<int> promise;
    Future<int> future = promise.future();

    bool ran = false;
    std::move(future).then([&](Result<int> result) {
        ran = true;
        REQUIRE_FALSE(result.is_ok());
        CHECK(result.error().code == Error::ReadOnlyLayer);
    });

    promise.reject(make_failure(Error::ReadOnlyLayer, "save"));
    CHECK(ran);
}

TEST_CASE("Future<void>::ready needs no value", "[future]") {
    auto future = Future<void>::ready();
    CHECK(future.is_ready());
    bool ran = false;
    std::move(future).then([&](Result<void> result) {
        ran = true;
        CHECK(result.is_ok());
    });
    CHECK(ran);
}

TEST_CASE("Promise<void>::resolve needs no value", "[future]") {
    Promise<void> promise;
    Future<void> future = promise.future();
    bool ran = false;
    std::move(future).then([&](Result<void> result) {
        ran = true;
        CHECK(result.is_ok());
    });
    CHECK_FALSE(ran);
    promise.resolve();
    CHECK(ran);
}
