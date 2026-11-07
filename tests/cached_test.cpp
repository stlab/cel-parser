// SPDX-License-Identifier: BSL-1.0
#include "../examples/cached_decoded.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("decoded scalar clients retain prepared programs and preserve caller prefixes") {
    const auto programs = [] {
        const std::array<std::string, 3> decoded{"2f64+\n3f64*4f64", "if false {0f64} else {9f64}",
                                                 "(|| 7f64)()"};
        return client::compile_decoded(
            std::array<std::string_view, 3>{decoded[0], decoded[1], decoded[2]});
    }();
    cel::raw_stack stack;
    stack.push(true);
    client::reserve(programs, stack);
    for (int repeat = 0; repeat != 5; ++repeat) {
        CHECK(programs[0].call(stack) == 14);
        CHECK(programs[1].call(stack) == 9);
        CHECK(programs[2].call(stack) == 7);
        CHECK(stack.depth() == 1);
    }
    CHECK(stack.pop<bool>());
    CHECK(client::compile_decoded({}).empty());
    CHECK_THROWS_AS(client::compile_decoded(std::array<std::string_view, 1>{"[1f64]"}),
                    std::invalid_argument);
    CHECK_THROWS_AS(client::compile_decoded(std::array<std::string_view, 1>{"1f64+"}),
                    cel::parse_error);
}
