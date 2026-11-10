// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
#ifdef CEL_PARSER_LOOKUP_HPP
#error Token inclusion must not import lookup.
#endif
#include <cel/parser/lookup.hpp>
#ifdef CEL_PARSER_COMPILER_HPP
#error Lookup inclusion must not import the compiler.
#endif
#include <cel/parser/all.hpp>
#include <cel/parser/compiler.hpp>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("full stack umbrella compiles and executes with caller storage") {
    cel::parser parser(cel::op_lookup{});
    auto bound = parser.parse_str("1 + 2").prepare<double>();
    cel::raw_stack stack;
    CHECK(bound.call(stack) == 3.0);
}
