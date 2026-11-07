// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
#include <string>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace {
/// Owns tokens collected from the accepted current/advance interface.
/// Complexity: O(source bytes), O(token payloads and count) storage.
std::vector<cel::token> tokens(std::string_view source) {
    cel::lex_lexer lexer(source);
    std::vector<cel::token> result;
    while (!std::holds_alternative<cel::end_token>(lexer.current().value)) {
        result.push_back(lexer.current());
        lexer.advance();
    }
    result.push_back(lexer.current());
    return result;
}
/// Parses one complete type with source ownership outside the parser.
/// Complexity: O(source bytes + complete descriptor construction).
cel::value_type type(std::string_view source, const cel::op_lookup& lookup) {
    auto input = tokens(source);
    return cel::parse_type(input, lookup);
}
/// Provides a valid client leaf that is not an admitted callable parameter.
struct client {
    double number;
};
} // namespace

TEST_CASE("type productions preserve nesting nominal identity and singleton commas") {
    cel::op_lookup lookup;
    lookup.register_type<client>("Client");
    lookup.register_record("Record",
                           std::array{cel::record_field{"x", cel::value_type::leaf<double>()}});
    CHECK(type("bool", lookup).same_shape(cel::value_type::leaf<bool>()));
    CHECK(type("(f64)", lookup).same_shape(cel::value_type::leaf<double>()));
    CHECK(type("()", lookup).fields().empty());
    CHECK(type("(f64,)", lookup).fields().size() == 1);
    auto nested = type("[((bool,), [Client], Record)]", lookup);
    CHECK(nested.element().fields().size() == 3);
    CHECK(nested.element().fields()[1].element().same_shape(cel::value_type::leaf<client>()));
    CHECK(nested.element().fields()[2].record_identity() == "Record");
    for (auto source :
         {"(bool, f64,)", "[f64,]", "Fn((f64,), bool,)", "Fn((f64), bool)", "Fn((),)", "Fn()",
          "bool f64", "[]", "(,)", "[f64)", "f32", "String", "usize", "Unknown", "bool(f64)"}) {
        CHECK_THROWS_AS(type(source, lookup), cel::parse_error);
    }
}

TEST_CASE("Fn parameter admission is recursive while complete results remain unrestricted") {
    cel::op_lookup lookup;
    lookup.register_type<client>("Client");
    auto fn = type("Fn((bool, [[(f64, bool)]]), Fn((), [Client]))", lookup);
    CHECK(fn.parameters().size() == 2);
    CHECK(fn.result().result().element().same_shape(cel::value_type::leaf<client>()));
    CHECK(type("Fn((), Client)", lookup).parameters().empty());
    for (auto source : {"Fn((Client,), bool)", "Fn(([Client],), bool)", "Fn((Fn((), bool),), bool)",
                        "Fn(([(Client, bool)],), f64)"}) {
        CHECK_THROWS_AS(type(source, lookup), cel::parse_error);
    }
    CHECK(type("RangeInclusive(f64)", lookup).form() == cel::range_kind::range_inclusive);
    CHECK(type("RangeFull()", lookup).form() == cel::range_kind::range_full);
    CHECK_THROWS_AS(type("Range(bool)", lookup), cel::parse_error);
    lookup.register_record("BuiltinRecord",
                           std::array{cel::record_field{
                               "x", cel::value_type::array(cel::value_type::leaf<double>())}});
    CHECK(type("Fn((BuiltinRecord, RangeInclusive(f64)), bool)", lookup).parameters().size() == 2);
    lookup.register_record("ClientRecord",
                           std::array{cel::record_field{"x", cel::value_type::leaf<client>()}});
    CHECK_THROWS_AS(type("Fn((ClientRecord,), bool)", lookup), cel::parse_error);
    CHECK(type("Fn((), ClientRecord)", lookup).result().record_identity() == "ClientRecord");
    CHECK(type("Fn((), [Fn((), Client)])", lookup).result().element().kind() ==
          cel::type_kind::callable);
}

TEST_CASE("inline type parsing leaves host token and errors retain exact byte spans") {
    cel::op_lookup lookup;
    cel::lex_lexer text_cursor("[f64], next");
    CHECK(
        cel::parse_type(text_cursor, lookup).element().same_shape(cel::value_type::leaf<double>()));
    CHECK(std::get<cel::punctuation>(text_cursor.current().value).spelling() == ",");
    auto input = tokens("[f64], next");
    cel::token_cursor cursor(input);
    CHECK(cel::parse_type(cursor, lookup).element().same_shape(cel::value_type::leaf<double>()));
    CHECK(std::get<cel::punctuation>(cursor.current().value).spelling() == ",");
    try {
        (void)type("[Unknown]", lookup);
        FAIL("unknown type accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.span() == cel::source_span{1, 8});
    }
    auto invalid = std::array{cel::token{
        {20, 21}, cel::parse_error(cel::error_category::malformed_input, {20, 21}, "producer")}};
    try {
        (void)cel::parse_type(invalid, lookup);
        FAIL("producer error accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.span() == cel::source_span{20, 21});
        CHECK(std::string(error.what()) == "producer");
    }
    const auto deep = std::string(1000, '[') + "f64" + std::string(1000, ']');
    CHECK(type(std::string(127, '[') + "f64" + std::string(127, ']'), lookup).kind() ==
          cel::type_kind::array);
    try {
        (void)type(deep, lookup);
        FAIL("resource boundary ignored");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::resource_limit);
    }
}
