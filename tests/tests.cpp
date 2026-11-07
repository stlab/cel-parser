// SPDX-License-Identifier: BSL-1.0

#include <cel/parser.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/// Collects independently owned tokens through the forward-only interface.
std::vector<cel::token> collect(std::string_view text) {
    cel::lex_lexer lexer(text);
    std::vector<cel::token> result;
    while (!std::holds_alternative<cel::end_token>(lexer.current().value)) {
        result.push_back(lexer.current());
        lexer.advance();
    }
    result.push_back(lexer.current());
    return result;
}

TEST_CASE("bool specialization preserves exact and raw identifier names") {
    auto t = collect("true false True true_ r#true _");
    REQUIRE(t.size() == 7);
    CHECK(std::get<bool>(t[0].value));
    CHECK_FALSE(std::get<bool>(t[1].value));
    CHECK(std::get<cel::identifier>(t[2].value).spelling == "True");
    CHECK(std::get<cel::identifier>(t[3].value).spelling == "true_");
    CHECK(std::get<cel::identifier>(t[4].value).spelling == "r#true");
    CHECK(std::get<cel::identifier>(t[5].value).spelling == "_");
}

TEST_CASE("numbers retain scalar meaning and exact selector spelling") {
    for (auto [text, expected] :
         std::vector<std::pair<std::string, double>>{{"1", 1},
                                                     {"1.25", 1.25},
                                                     {"1f64", 1},
                                                     {"2.5f64", 2.5},
                                                     {"1e+2", 100},
                                                     {"1_e_+_2", 100},
                                                     {"1_000", 1000},
                                                     {"1.", 1},
                                                     {"0xff", 255},
                                                     {"0o_17", 15},
                                                     {"0b_101_", 5},
                                                     {"9007199254740993", 9007199254740992.0}}) {
        INFO(text);
        auto t = collect(text);
        REQUIRE(t.size() == 2);
        REQUIRE(std::holds_alternative<cel::numeric_literal>(t[0].value));
        auto const& number = std::get<cel::numeric_literal>(t[0].value);
        CHECK(number.value == expected);
        CHECK(number.spelling == text);
        CHECK(t[0].span.begin == 0);
        CHECK(t[0].span.end == text.size());
    }
    auto t = collect(".0.1 1..2 1.foo 0.1e2 0f64");
    REQUIRE(t.size() == 11);
    CHECK(std::get<cel::punctuation>(t[0].value).spelling() == ".");
    CHECK(std::get<cel::numeric_literal>(t[1].value).spelling == "0.1");
    CHECK(std::get<cel::numeric_literal>(t[1].value).form == cel::numeric_form::decimal);
    CHECK(std::get<cel::punctuation>(t[3].value).spelling() == "..");
    CHECK(std::get<cel::numeric_literal>(t[8].value).form == cel::numeric_form::exponent);
    CHECK(std::get<cel::numeric_literal>(t[9].value).suffix == "f64");
}

TEST_CASE("binary64 boundary conversion preserves Rust infinity and zero results") {
    auto t = collect("1e9999 1e-9999 1.7976931348623157e308 "
                     "1.7976931348623159e308 5e-324 2e-324 0e9999");
    REQUIRE(t.size() == 8);
    CHECK(std::isinf(std::get<cel::numeric_literal>(t[0].value).value));
    CHECK(std::get<cel::numeric_literal>(t[1].value).value == 0);
    CHECK(std::get<cel::numeric_literal>(t[2].value).value == std::numeric_limits<double>::max());
    CHECK(std::isinf(std::get<cel::numeric_literal>(t[3].value).value));
    CHECK(std::get<cel::numeric_literal>(t[4].value).value ==
          std::numeric_limits<double>::denorm_min());
    CHECK(std::get<cel::numeric_literal>(t[5].value).value == 0);
    CHECK(std::get<cel::numeric_literal>(t[6].value).value == 0);
    auto huge = collect("0x" + std::string(1000, 'f'));
    CHECK(std::isinf(std::get<cel::numeric_literal>(huge[0].value).value));
    auto extremes = collect("0.0001e999999999999999999999 1000e-999999999999999999999");
    REQUIRE(extremes.size() == 3);
    CHECK(std::isinf(std::get<cel::numeric_literal>(extremes[0].value).value));
    CHECK(std::get<cel::numeric_literal>(extremes[1].value).value == 0);
}

TEST_CASE("radix integers round ties without narrowing and retain exact spelling") {
    constexpr std::array<std::pair<std::string_view, double>, 10> cases{{
        {"0b100000000000000000000000000000000000000000000000000001", 9007199254740992.0},
        {"0o400000000000000001", 9007199254740992.0},
        {"0x20000000000001", 9007199254740992.0},
        {"0b100000000000000000000000000000000000000000000000000011", 9007199254740996.0},
        {"0o400000000000000003", 9007199254740996.0},
        {"0x20000000000003", 9007199254740996.0},
        {"0b100000000000000000000000000000000000000000000000000000000000000000000000000000001",
         0x1p80},
        {"0o400000000000000000000000001", 0x1p80},
        {"0x100000000000000000001", 0x1p80},
        {"0o12_345", 5349.0},
    }};
    for (auto [text, expected] : cases) {
        INFO(text);
        const auto tokens = collect(text);
        REQUIRE(tokens.size() == 2);
        REQUIRE(std::holds_alternative<cel::numeric_literal>(tokens[0].value));
        const auto& number = std::get<cel::numeric_literal>(tokens[0].value);
        CHECK(number.value == expected);
        CHECK(number.spelling == text);
    }
}

TEST_CASE("joint compounds use maximal supported matching and full byte spans") {
    for (auto text :
         {"&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "->", "=>", ":=", "..", "..="}) {
        auto t = collect(text);
        REQUIRE(t.size() == 2);
        CHECK(std::get<cel::punctuation>(t[0].value).spelling() == text);
        CHECK(t[0].span.end == std::string_view(text).size());
    }
    auto t = collect("& & &/**/ & &//x\n& .. = ..== +++ ::");
    REQUIRE(t.size() == 16);
    CHECK(std::get<cel::punctuation>(t[6].value).spelling() == "..");
    CHECK(std::get<cel::punctuation>(t[8].value).spelling() == "..=");
    CHECK(std::get<cel::punctuation>(t[9].value).spelling() == "=");
}

TEST_CASE("comments distinguish doc metadata and preserve nested contents") {
    auto t = collect("//// ordinary\n/**/ /*** ordinary */ /// outer\r\n"
                     "//!inner\n/** block /*nested*/ text */ /*! inside */ true");
    REQUIRE(t.size() == 6);
    CHECK(std::get<cel::doc_comment>(t[0].value).text == " outer");
    CHECK_FALSE(std::get<cel::doc_comment>(t[0].value).inner);
    CHECK(std::get<cel::doc_comment>(t[1].value).inner);
    CHECK(std::get<cel::doc_comment>(t[2].value).text == " block /*nested*/ text ");
    CHECK(std::get<cel::doc_comment>(t[3].value).text == " inside ");
    CHECK(std::get<bool>(t[4].value));
}

TEST_CASE("comment markers at EOF remain bounded") {
    CHECK(collect("//").size() == 1);
    auto t = collect("/*");
    REQUIRE(t.size() == 2);
    CHECK(std::get<cel::parse_error>(t[0].value).category() ==
          cel::error_category::malformed_input);
}

TEST_CASE("Unicode identifiers and producer whitespace retain byte boundaries") {
    auto t = collect("α\u0301 \u00a0\u200e\u200f日本 \U0003d000");
    REQUIRE(t.size() == 4);
    CHECK(std::get<cel::identifier>(t[0].value).spelling == "α\u0301");
    CHECK(t[0].span.begin == 0);
    CHECK(t[0].span.end == 4);
    CHECK(std::get<cel::identifier>(t[1].value).spelling == "日本");
    CHECK(t[1].span.begin == 13);
    CHECK(t[1].span.end == 19);
    CHECK(std::get<cel::identifier>(t[2].value).spelling == "\U0003d000");
}

TEST_CASE("diagnostics identify unsupported versus malformed input") {
    for (auto text : {"1f32", "1u8", "1.0i32", "1名字", "\"text\"", "'x'", "b\"x\"", "r#\"x\"#",
                      "br##\"x\"##", "c\"x\""}) {
        INFO(text);
        auto t = collect(text);
        REQUIRE(t.size() == 2);
        REQUIRE(std::holds_alternative<cel::parse_error>(t[0].value));
        auto const& error = std::get<cel::parse_error>(t[0].value);
        CHECK(error.category() == cel::error_category::unsupported_literal);
        CHECK(error.span().begin == 0);
        CHECK(error.span().end == std::string_view(text).size());
        CHECK(error.span() == (cel::source_span{0, std::string_view(text).size()}));
        CHECK((error.what()[0] != '\0'));
    }
    auto incomplete = collect("1e+");
    REQUIRE(incomplete.size() == 2);
    auto const& error = std::get<cel::parse_error>(incomplete[0].value);
    CHECK(error.category() == cel::error_category::unsupported_literal);
    CHECK(error.span().end == 2);
    for (auto text :
         {"0x", "0b2", "\"unfinished", "/*unfinished", "///bare\rCR", "\u0301", "😀", "r#self"}) {
        INFO(text);
        auto t = collect(text);
        REQUIRE(std::holds_alternative<cel::parse_error>(t[t.size() - 2].value));
        CHECK(std::get<cel::parse_error>(t[t.size() - 2].value).category() ==
              cel::error_category::malformed_input);
    }
}

TEST_CASE("malformed UTF8 locates the first invalid byte even in trivia") {
    for (auto bytes :
         {std::string("\x80", 1), std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
          std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)}) {
        auto t = collect("/*" + bytes);
        REQUIRE(t.size() == 2);
        auto const& error = std::get<cel::parse_error>(t[0].value);
        CHECK(error.category() == cel::error_category::malformed_input);
        CHECK(error.span().begin == 2);
        CHECK(error.span().end == 3);
    }
}

TEST_CASE("delimiter checking handles deep valid and malformed input iteratively") {
    auto t = collect("({[]})");
    REQUIRE(t.size() == 7);
    CHECK(std::get<cel::open_delimiter>(t[0].value).kind == cel::delimiter::parenthesis);
    CHECK(std::get<cel::close_delimiter>(t[5].value).kind == cel::delimiter::parenthesis);
    for (auto text : {")", "([)]", "{"}) {
        auto errors = collect(text);
        auto const& error = std::get<cel::parse_error>(errors[errors.size() - 2].value);
        CHECK(error.category() == cel::error_category::delimiter_error);
    }
    std::string deep(50000, '(');
    auto malformed = collect(deep);
    CHECK(std::get<cel::parse_error>(malformed[malformed.size() - 2].value).span().begin == 49999);
    deep += std::string(50000, ')');
    CHECK(collect(deep).size() == 100001);
    std::string comments;
    for (int i = 0; i != 50000; ++i)
        comments += "/*";
    for (int i = 0; i != 50000; ++i)
        comments += "*/";
    CHECK(collect(comments).size() == 1);
}

TEST_CASE("owned payload copies survive sources adapters and mutations") {
    std::vector<cel::token> t;
    {
        std::string source = "identifier 1_2f64 ///text";
        t = collect(source);
        source.assign(source.size(), 'x');
    }
    auto copy = t;
    std::get<cel::identifier>(copy[0].value).spelling = "changed";
    CHECK(std::get<cel::identifier>(t[0].value).spelling == "identifier");
    CHECK(std::get<cel::numeric_literal>(t[1].value).spelling == "1_2f64");
    CHECK(std::get<cel::doc_comment>(t[2].value).text == "text");
    cel::lex_lexer lexer(std::string("temporary"));
    CHECK(std::get<cel::identifier>(lexer.current().value).spelling == "temporary");
    std::string lazy_source = "alpha beta";
    cel::lex_lexer lazy(lazy_source);
    lazy_source.assign(lazy_source.size(), 'x');
    lazy.advance();
    CHECK(std::get<cel::identifier>(lazy.current().value).spelling == "beta");
}

TEST_CASE("flattened producer cursor preserves current payload error and EOF laws") {
    std::vector<cel::token> tokens{{{4, 5}, true}, {{6, 7}, cel::identifier{"x"}}};
    cel::token_cursor cursor(tokens);
    CHECK(std::get<bool>(cursor.current().value));
    cursor.advance();
    CHECK(std::get<cel::identifier>(cursor.current().value).spelling == "x");
    cursor.advance();
    CHECK(std::holds_alternative<cel::end_token>(cursor.current().value));
    CHECK(cursor.current().span.end == 7);
    cursor.advance();
    CHECK(cursor.current().span.begin == 7);
    cel::token_cursor empty(std::span<const cel::token>{});
    CHECK(std::holds_alternative<cel::end_token>(empty.current().value));
    std::vector<cel::token> errors{
        {{2, 3}, cel::parse_error(cel::error_category::malformed_input, {2, 3}, "producer error")},
        {{4, 5}, true}};
    cel::token_cursor failed(errors);
    CHECK(std::get<cel::parse_error>(failed.current().value).span().begin == 2);
    failed.advance();
    CHECK(std::holds_alternative<cel::end_token>(failed.current().value));
    cel::lex_lexer eof(" \n");
    CHECK(eof.current().span.begin == 2);
    eof.advance();
    CHECK(eof.current().span.end == 2);
    std::array<cel::token, 2> explicit_end{cel::token{{8, 8}, cel::end_token{}},
                                           cel::token{{9, 10}, true}};
    cel::token_cursor terminal(explicit_end);
    terminal.advance();
    CHECK(std::holds_alternative<cel::end_token>(terminal.current().value));
    CHECK(terminal.current().span.begin == 8);
    std::array<cel::token, 2> invisible{
        cel::token{{2, 5}, cel::open_delimiter{cel::delimiter::none}},
        cel::token{{2, 5}, cel::close_delimiter{cel::delimiter::none}}};
    cel::token_cursor groups(invisible);
    CHECK(std::get<cel::open_delimiter>(groups.current().value).kind == cel::delimiter::none);
    groups.advance();
    CHECK(std::get<cel::close_delimiter>(groups.current().value).kind == cel::delimiter::none);
    CHECK(groups.current().span == (cel::source_span{2, 5}));
}

TEST_CASE("inline punctuation validates caller input and owns its bytes") {
    std::string source = "..=";
    cel::punctuation punct(source);
    source.assign(3, 'x');
    CHECK(punct.spelling() == "..=");
    for (auto invalid : {"", "....", "abc", "α"}) {
        CHECK_THROWS_AS(cel::punctuation{invalid}, std::invalid_argument);
    }
}
