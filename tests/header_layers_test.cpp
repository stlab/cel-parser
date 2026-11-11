// SPDX-License-Identifier: BSL-1.0
#include <array>
#include <cel/parser.hpp>
#include <string_view>
#include <utility>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

TEST_CASE("public parser header compiles UTF-8 and reuses caller storage") {
    cel::parser parser(cel::op_lookup{});
    auto bound = parser.parse_str("/* \xCE\xB1 */ 1.0 + 2.0").prepare<double>();
    cel::raw_stack stack;
    stack.push(true);
    for (int repeat = 0; repeat != 3; ++repeat)
        CHECK(bound.call(stack) == 3.0);
    CHECK(stack.pop<bool>());
}

TEST_CASE("public compilation preserves malformed UTF-8 byte diagnostics") {
    cel::parser parser(cel::op_lookup{});
    const std::array cases{
        std::pair{std::string_view{"\xFF"}, cel::source_span{0, 1}},
        std::pair{std::string_view{"/* \xCE\xB1 */ \xFF"}, cel::source_span{9, 10}}};
    for (const auto& [source, expected] : cases) {
        try {
            (void)parser.parse_str(source);
            FAIL("malformed UTF-8 was accepted");
        } catch (const cel::parse_error& error) {
            CHECK(error.category() == cel::error_category::malformed_input);
            CHECK(error.span() == expected);
        }
    }
}
