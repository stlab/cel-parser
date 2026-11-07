// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
#include <cel/runtime.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace {
/// Collects an external producer stream for the public borrowed-token boundary.
/// Complexity: O(source bytes + tokens) time and storage.
std::vector<cel::token> tokens(std::string_view source) {
    cel::lex_lexer lexer(source);
    std::vector<cel::token> result;
    do {
        result.push_back(lexer.current());
        if (std::holds_alternative<cel::end_token>(lexer.current().value)) break;
        lexer.advance();
    } while (true);
    return result;
}
/// Compiles and executes text with a fresh caller stack.
/// Complexity: O(compilation + execution).
cel::value evaluate(cel::parser& parser, std::string_view text) {
    cel::raw_stack stack;
    return parser.parse_str(text).execute(stack, {});
}
/// Checks an exact source diagnostic through the public compiler.
/// Complexity: O(compilation).
void error_at(cel::parser& parser,
              std::string_view text,
              cel::error_category category,
              cel::source_span span) {
    try {
        (void)parser.parse_str(text);
        FAIL("invalid expression accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == category);
        CHECK(error.span() == span);
    }
}
/// Identifies named callback operations without adding implicit arguments.
struct marker {};
/// Provides a valid client leaf, excluded from closure parameters.
struct client {
    double number;
};
} // namespace

TEST_CASE("retained precedence associativity casts and IEEE numeric spellings") {
    cel::parser parser{cel::op_lookup{}};
    for (const auto& [text, expected] :
         std::array{std::pair{"1 + 2 * 3", 7.0}, std::pair{"10 - 3 - 2", 5.0},
                    std::pair{"20 / 2 / 2", 5.0}, std::pair{"-2 as f64 * 3", -6.0},
                    std::pair{"1 as f64 as f64 + 2", 3.0}, std::pair{"0x10 + 0b10", 18.0},
                    std::pair{"2f64 + 3", 5.0}, std::pair{"-5 % 2", -1.0}}) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).as<double>() == expected);
    }
    CHECK(evaluate(parser, "!false && 1 + 2 == 3 || false").as<bool>());
    CHECK(evaluate(parser, "true as bool == !false").as<bool>());
    for (auto text : {"1 < 2", "2 > 1", "1 <= 1", "1 >= 1", "1 != 2", "true != false"}) {
        CHECK(evaluate(parser, text).as<bool>());
    }
    CHECK(std::isinf(evaluate(parser, "1e9999").as<double>()));
    CHECK(evaluate(parser, "1e-9999").as<double>() == 0.0);
    CHECK(evaluate(parser, "5e-324").as<double>() == std::numeric_limits<double>::denorm_min());
    CHECK(std::signbit(evaluate(parser, "-0").as<double>()));
    CHECK(std::isnan(evaluate(parser, "0 / 0").as<double>()));
    for (auto text :
         {"1 < 2 < 3", "true == false == true", "+1", "true as f64", "1 as bool", "1 as (f64)",
          "1f32", "1 | 2", "1 & 2", "1 << 2", "1 ^ 2", "else", "as", "1 +"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("standard numeric names require explicit installation and use source call selectors") {
    cel::parser default_parser{cel::op_lookup{}};
    CHECK_THROWS_AS(default_parser.parse_str("sqrt(4)"), cel::parse_error);
    cel::op_lookup lookup;
    cel::install_standard_library(lookup);
    cel::parser parser(std::move(lookup));
    for (const auto& [text, expected] : std::array{
             std::pair{"min(1,2)", 1.0}, std::pair{"max(1,2)", 2.0}, std::pair{"clamp(4,1,3)", 3.0},
             std::pair{"abs(-2)", 2.0}, std::pair{"signum(-2)", -1.0}, std::pair{"sqrt(4)", 2.0},
             std::pair{"floor(2.5)", 2.0}, std::pair{"ceil(2.5)", 3.0},
             std::pair{"trunc(-2.5)", -2.0}, std::pair{"fract(2.5)", 0.5},
             std::pair{"round_ties_even(2.5)", 2.0}, std::pair{"(-2).abs()", 2.0}}) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).as<double>() == expected);
    }
    CHECK_THROWS_AS(evaluate(parser, "clamp(1,3,2)"), std::invalid_argument);
    CHECK_THROWS_AS(parser.parse_str("round(2.5)"), cel::parse_error);
}

TEST_CASE("tuple group unit array annotations and strict separators") {
    cel::parser parser{cel::op_lookup{}};
    CHECK(evaluate(parser, "()").size() == 0);
    CHECK(evaluate(parser, "(1 + 2)").as<double>() == 3);
    CHECK(evaluate(parser, "(1,)").at(0).as<double>() == 1);
    CHECK(evaluate(parser, "((), (true,), [[1, 2], [3]])").at(2).at(1).at(0).as<double>() == 3);
    CHECK(evaluate(parser, "[]: [f64]").size() == 0);
    CHECK(evaluate(parser, "[1, 2]: [f64]").at(1).as<double>() == 2);
    CHECK(evaluate(parser, "[[]: [bool]]").at(0).size() == 0);
    for (auto text : {"[]", "[1,]", "(1, 2,)", "(,)", "[,]", "[1 2]", "(1 2)", "[1]: f64",
                      "[true]: [f64]", "[1, true]", "[:[f64]]", "[[]: [bool]]: [[f64]]", "([1)"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    error_at(parser, "[]:(f64)", cel::error_category::type_error, {3, 8});
    error_at(parser, "[]:((f64,[bool]))   ", cel::error_category::type_error, {3, 17});
    error_at(parser, "[true]:[f64]", cel::error_category::type_error, {0, 12});
    error_at(parser, "[true]:([f64])   ", cel::error_category::type_error, {0, 14});
    error_at(parser, "[[]:[bool]]:([[f64]])   ", cel::error_category::type_error, {0, 21});
    error_at(parser, "[1,true]:([f64])   ", cel::error_category::type_error, {0, 16});
}

TEST_CASE("tuple selectors use retained raw digits rather than double conversion") {
    cel::parser parser{cel::op_lookup{}};
    for (auto text : {"((1, 2),).0.1", "(1, 2).0x1", "(1, 2).0b1", "(1, 2).0o1", "(1, 2).0_1",
                      "((1, 2),).0_0.0_1"}) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).as<double>() == 2);
    }
    for (auto text : {"(1,).0f64", "(1,).0e0", "(1,).0.1e0", "(1,).1.",
                      "(1,).184467440737095516160", "(1,).2", "1.0.0", "(1,).-1", "(1,)."}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    error_at(parser, "(true).0", cel::error_category::type_error, {0, 8});
    error_at(parser, "(1,).2", cel::error_category::type_error, {0, 6});
    error_at(parser, "((1,),).0.2", cel::error_category::type_error, {0, 11});
    error_at(parser, "(1,).0f64", cel::error_category::malformed_input, {5, 9});
    error_at(parser, "(1,).0e0", cel::error_category::malformed_input, {5, 8});
}

TEST_CASE("explicit C++ array indexing validates before structural access") {
    cel::parser parser{cel::op_lookup{}};
    CHECK(evaluate(parser, "[[1, 2], [3, 4]][1][0]").as<double>() == 3);
    CHECK(evaluate(parser, "[4][-0]").as<double>() == 4);
    for (auto text : {"[1][-1]", "[1][0.5]", "[1][1]", "[1][1e9999]", "[1][0/0]",
                      "[1][18446744073709551616]", "([]:[f64])[0]"}) {
        CAPTURE(text);
        auto program = parser.parse_str(text);
        cel::raw_stack stack;
        stack.push(99.0);
        CHECK_THROWS_AS(program.execute(stack, {}), std::out_of_range);
        CHECK(stack.pop<double>() == 99.0);
    }
    for (auto text : {"[1][true]", "[1][]", "[1][0, 0]", "1[0]"}) {
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("all six range forms preserve complete type and endpoint order") {
    cel::parser parser{cel::op_lookup{}};
    const auto forms = std::array{std::pair{"1 + 2..4 * 2", cel::range_kind::range},
                                  std::pair{"1..=3", cel::range_kind::range_inclusive},
                                  std::pair{"1..", cel::range_kind::range_from},
                                  std::pair{"..3", cel::range_kind::range_to},
                                  std::pair{"..=3", cel::range_kind::range_to_inclusive},
                                  std::pair{"..", cel::range_kind::range_full}};
    for (const auto& [text, kind] : forms) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).type().form() == kind);
    }
    auto range = evaluate(parser, "1 + 2..4 * 2");
    CHECK(range.at(0).as<double>() == 3);
    CHECK(range.at(1).as<double>() == 8);
    for (auto text : {"..=", "1..=", "1..2..3", "true..false", "1==2..3==4"}) {
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    for (auto text : {"..else", "1..else", "..as", "1..as"}) {
        const auto input = tokens(text);
        cel::token_cursor cursor(input);
        CHECK_THROWS_AS(parser.parse_expression(cursor), cel::parse_error);
    }
}

TEST_CASE("lazy branches compile both sides without executing callbacks") {
    std::vector<int> trace;
    cel::op_lookup lookup;
    lookup.register_function("hit", marker{});
    lookup.register_op<bool, double>("hit", [&trace](double n) {
        trace.push_back(static_cast<int>(n));
        return n > 0;
    });
    lookup.register_function("explode", client{});
    lookup.register_op<bool>("explode", []() -> bool { throw std::domain_error("selected"); });
    cel::parser parser(std::move(lookup));
    auto logical = parser.parse_str("hit(1) || explode()");
    CHECK(trace.empty());
    cel::raw_stack stack;
    stack.push(std::tuple{true, 99.0});
    CHECK(logical.execute(stack, {}).as<bool>());
    CHECK(trace == std::vector<int>{1});
    CHECK(evaluate(parser, "false && explode()").as<bool>() == false);
    CHECK(evaluate(parser, "if true { true } else { explode() }").as<bool>());
    CHECK(evaluate(parser, "if false { explode() } else if true { hit(2) } else { false }")
              .as<bool>());
    CHECK(trace == std::vector<int>{1, 2});
    CHECK_THROWS_AS(evaluate(parser, "true && explode()"), std::domain_error);
    CHECK_THROWS_AS(evaluate(parser, "if true { explode() } else { false }"), std::domain_error);
    auto selected = parser.parse_str("((), if true { explode() } else { false })");
    CHECK_THROWS_AS(selected.execute(stack, {}), std::domain_error);
    CHECK(stack.depth() == 1);
    CHECK(stack.pop<std::tuple<bool, double>>() == std::tuple{true, 99.0});
    CHECK(evaluate(parser, "if false { () }").size() == 0);
    CHECK(evaluate(parser, "if true { () }").size() == 0);
    for (auto text : {"if true { 1 }", "if true { 1 } else { false }", "false && 1", "if 1 { () }",
                      "if true {}", "if true {1} else {}", "if true {1} else 2", "if true 1",
                      "if true {1; 2}"}) {
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("registered names member selectors record permutation and source evaluation") {
    std::vector<double> trace;
    cel::op_lookup lookup;
    lookup.register_record("Pair",
                           std::array{cel::record_field{"x", cel::value_type::leaf<double>()},
                                      cel::record_field{"y", cel::value_type::leaf<double>()}});
    lookup.register_record("Empty", {});
    lookup.register_function("tap", marker{});
    lookup.register_op<double, double>("tap", [&trace](double x) {
        trace.push_back(x);
        return x;
    });
    lookup.register_op<double, double, double>("plus", [](double x, double y) { return x + y; });
    lookup.register_function("named", client{});
    lookup.register_op<double, double>("named", [](double x) { return x * 10; });
    cel::parser parser(std::move(lookup));
    auto program = parser.parse_str("Pair { y: tap(2), x: tap(1) }.x.plus(3)");
    CHECK(trace.empty());
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).as<double>() == 4);
    CHECK(trace == std::vector<double>{2, 1});
    CHECK(evaluate(parser, "Pair {y: 2, x: 1}.y").as<double>() == 2);
    CHECK(evaluate(parser, "Empty {}").size() == 0);
    CHECK(evaluate(parser, "1.named()").as<double>() == 10);
    CHECK(evaluate(parser, "named(2)").as<double>() == 20);
    CHECK_THROWS_AS(parser.parse_str("1.named()(2)"), cel::parse_error);
    CHECK_THROWS_AS(parser.parse_str("plus(1, 2)"), cel::parse_error);
    error_at(parser, "Pair{x:true,y:2}", cel::error_category::type_error, {5, 6});
    error_at(parser, "Unknown{}", cel::error_category::type_error, {0, 9});
    for (auto text :
         {"Pair{x:1}", "Pair{x:1,x:2}", "Pair{z:1,y:2}", "Pair{x:true,y:2}", "Pair{x:1,y:2,}",
          "Pair{x 1,y:2}", "Unknown{}", "named(1,)", "1.plus(2,)", "Pair{x:1,y:2}.missing"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE(
    "closures admit complete arguments client results higher order and empty callable composites") {
    cel::op_lookup lookup;
    lookup.register_type<client>("Client");
    lookup.register_function("leaf", marker{});
    lookup.register_op<client>("leaf", [] { return client{8}; });
    lookup.register_record(
        "Box", std::array{cel::record_field{
                   "f", cel::value_type::callable({}, cel::value_type::leaf<double>())}});
    cel::parser parser(std::move(lookup));
    CHECK(evaluate(parser, "(|x:f64,y:bool| if y {x+1} else {0})(4,true)").as<double>() == 5);
    CHECK(
        evaluate(parser, "(|x:((f64,bool),[f64])| x.0.0 + x.1[0])(((1,true),[2]))").as<double>() ==
        3);
    CHECK(evaluate(parser, "(|| leaf())()").as<client>().number == 8);
    CHECK(evaluate(parser, "(|| [leaf()])()").at(0).as<client>().number == 8);
    CHECK(evaluate(parser, "[leaf()]:[Client]").at(0).as<client>().number == 8);
    CHECK(evaluate(parser, "[]:[Client]").size() == 0);
    CHECK(evaluate(parser, "(|| || 7)()()").as<double>() == 7);
    CHECK(evaluate(parser, "(|| [|| 7])()[0]()").as<double>() == 7);
    CHECK(evaluate(parser, "(if true {||7} else {||8})()").as<double>() == 7);
    CHECK(evaluate(parser, "(|x:()|x)(())").size() == 0);
    CHECK(evaluate(parser, "([|| 7]:[Fn((),f64)])[0]()").as<double>() == 7);
    CHECK(evaluate(parser, "(Box{f:|| 9}.f)()").as<double>() == 9);
    CHECK(evaluate(parser, "Box{f:|| 9}.f()()").as<double>() == 9);
    CHECK(evaluate(parser, "[]:[Fn((),f64)]").size() == 0);
    CHECK(evaluate(parser, "[]:[([Fn((),f64)],bool)]").size() == 0);
    CHECK(evaluate(parser, "[]:[Box]").size() == 0);
    CHECK(evaluate(parser, "(|| []:[Fn((),f64)])()").size() == 0);
    CHECK(evaluate(parser, "[]:[Fn((),Fn((),f64))]").size() == 0);
    CHECK(evaluate(parser, "(|r:RangeFull|r)(..)").type().form() == cel::range_kind::range_full);
    for (auto text :
         {"|x:Client| 1", "|x:[Client]| 1", "|f:Fn((),f64)| 1", "|x:f64,x:f64| x", "|if:f64| 1",
          "|else:f64| 1", "|as:f64| 1", "|true:f64| 1", "|x:f64,| 1", "| | 1", "||", "|x f64| x",
          "(|x:f64| x)(true)", "(|| 1)(2)", "[||1]:[Fn((),bool)]"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("independent closure scope isolation restores scopes after success and failure") {
    cel::op_lookup lookup;
    lookup.push_library_scope(
        [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
            if (name != "library" || n != 0) return false;
            b.just(cel::value::from(7.0));
            return true;
        });
    lookup.push_scope(
        [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
            if (name != "ambient" || n != 0) return false;
            b.just(cel::value::from(3.0));
            return true;
        });
    cel::parser parser(std::move(lookup));
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "(|| library)()").as<double>() == 7);
    CHECK_THROWS_AS(parser.parse_str("|| ambient"), cel::parse_error);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK_THROWS_AS(parser.parse_str("|x:f64| || x"), cel::parse_error);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "(|ambient:f64| ambient)(9)").as<double>() == 9);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "if true {ambient} else {0}").as<double>() == 3);
    CHECK(evaluate(parser, "(|ambient:bool| if ambient {1} else {2})(true)").as<double>() == 1);
}

TEST_CASE("semantic record parameters custom members and explicit scalar call operations") {
    cel::op_lookup lookup;
    lookup.register_type<double>("Alias");
    lookup.register_record("Pair",
                           std::array{cel::record_field{"x", cel::value_type::leaf<double>()},
                                      cel::record_field{"y", cel::value_type::leaf<double>()}});
    const auto pair = lookup.find_type("Pair");
    if (!pair) {
        FAIL("registered nominal type missing");
        return;
    }
    lookup.register_operation("x", std::array{*pair}, cel::value_type::leaf<double>(),
                              [](cel::dyn_segment& b, cel::source_span) { b.project_record("y"); });
    lookup.register_op<double, double, double>("()", [](double x, double y) { return x + y; });
    lookup.register_op<double, double, double>("|", [](double x, double y) { return x + y; });
    cel::parser parser(std::move(lookup));
    CHECK(evaluate(parser, "Pair{x:1,y:2}.x").as<double>() == 2);
    CHECK(evaluate(parser, "(|p:Pair|p.x)(Pair{x:1,y:2})").as<double>() == 2);
    CHECK(evaluate(parser, "(2)(3)").as<double>() == 5);
    CHECK(evaluate(parser, "1 | 2 * 3").as<double>() == 7);
    CHECK(evaluate(parser, "[1]:[Alias]").at(0).as<double>() == 1);
    CHECK_THROWS_AS(parser.parse_str("1 as Alias"), cel::parse_error);
}

TEST_CASE("client emitter exceptions and recursive compiler access are not parse diagnostics") {
    cel::parser* active = nullptr;
    cel::op_lookup lookup;
    lookup.register_operation("fail", std::array{cel::value_type::leaf<double>()},
                              cel::value_type::leaf<double>(),
                              [](cel::dyn_segment&, cel::source_span) {
                                  throw std::invalid_argument("client emission failure");
                              });
    lookup.register_operation(
        "reenter", std::array{cel::value_type::leaf<double>()}, cel::value_type::leaf<double>(),
        [&active](cel::dyn_segment&, cel::source_span) { (void)active->parse_str("1"); });
    cel::parser parser(std::move(lookup));
    active = &parser;
    CHECK_THROWS_AS(parser.parse_str("1.fail()"), std::invalid_argument);
    CHECK_THROWS_AS(parser.parse_str("1.reenter()"), std::logic_error);
    CHECK_THROWS_AS(parser.parse_str("|| 1.fail()"), std::invalid_argument);
    CHECK(evaluate(parser, "1+2").as<double>() == 3);
}

TEST_CASE(
    "compiled values own captures and metadata after parser destruction and reuse prefixed stack") {
    auto program = [] {
        cel::op_lookup lookup;
        lookup.register_function("owned", marker{});
        lookup.register_op<double>("owned",
                                   [number = std::make_unique<double>(12)] { return *number; });
        cel::parser parser(std::move(lookup));
        return parser.parse_str("((), [|| owned()], || || owned())");
    }();
    cel::raw_stack stack;
    stack.push(5.0);
    for (int repeat = 0; repeat != 3; ++repeat) {
        auto result = program.execute(stack, {});
        auto fn = cel::callable::from_value(result.at(1).at(0));
        CHECK(fn.call(stack, {}).as<double>() == 12);
        auto outer = cel::callable::from_value(result.at(2));
        auto inner = cel::callable::from_value(outer.call(stack, {}));
        CHECK(inner.call(stack, {}).as<double>() == 12);
        CHECK(stack.depth() == 1);
    }
    CHECK(stack.pop<double>() == 5);
    auto scalar = [] {
        cel::parser parser(cel::op_lookup{});
        return parser.parse_str("2+3*4");
    }();
    auto prepared = scalar.prepare<double>();
    stack.push(true);
    CHECK(prepared.call(stack) == 14);
    CHECK(prepared.call(stack) == 14);
    CHECK(stack.pop<bool>());
}

TEST_CASE(
    "text and token methods reject trailing input inline leaves host token diagnostics are owned") {
    cel::parser parser{cel::op_lookup{}};
    auto input = tokens("1+2, host");
    cel::token_cursor cursor(input);
    auto program = parser.parse_expression(cursor);
    CHECK(std::get<cel::punctuation>(cursor.current().value).spelling() == ",");
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).as<double>() == 3);
    CHECK_THROWS_AS(parser.parse_tokens(input), cel::parse_error);
    CHECK(parser.parse_tokens(tokens("(1,2).1")).execute(stack, {}).as<double>() == 2);
    error_at(parser, "1 2", cel::error_category::malformed_input, {2, 3});
    error_at(parser, "[1,]", cel::error_category::malformed_input, {3, 4});
    error_at(parser, "(1]", cel::error_category::delimiter_error, {2, 3});
    error_at(parser, "missing", cel::error_category::lookup_error, {0, 7});
    error_at(parser, "[1,true]:[f64]   ", cel::error_category::type_error, {0, 14});
    for (const auto& [text, expected] :
         std::array{std::pair{"[]:(f64)   , host", cel::source_span{3, 8}},
                    std::pair{"[true]:([f64])   , host", cel::source_span{0, 14}},
                    std::pair{"[[]:[bool]]:([[f64]])   , host", cel::source_span{0, 21}}}) {
        CAPTURE(std::string_view(text));
        const auto annotated = tokens(text);
        cel::token_cursor prefix(annotated);
        try {
            (void)parser.parse_expression(prefix);
            FAIL("invalid annotation accepted");
        } catch (const cel::parse_error& error) {
            CHECK(error.category() == cel::error_category::type_error);
            CHECK(error.span() == expected);
        }
        CHECK(std::get<cel::punctuation>(prefix.current().value).spelling() == ",");
    }
    auto invalid = std::array{cel::token{
        {20, 21},
        cel::parse_error(cel::error_category::unsupported_literal, {20, 21}, "producer")}};
    try {
        (void)parser.parse_tokens(invalid);
        FAIL("producer diagnostic ignored");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::unsupported_literal);
        CHECK(error.span() == cel::source_span{20, 21});
        CHECK(std::string(error.what()) == "producer");
    }
    const auto malformed = std::array{
        cel::token{{10, 11}, cel::open_delimiter{cel::delimiter::parenthesis}},
        cel::token{{11, 12}, cel::numeric_literal{1, "1", "", cel::numeric_form::integer}},
        cel::token{{12, 13}, cel::close_delimiter{cel::delimiter::bracket}}};
    CHECK_THROWS_AS(parser.parse_tokens(malformed), cel::parse_error);
    const auto invisible =
        std::array{cel::token{{0, 0}, cel::open_delimiter{cel::delimiter::none}},
                   cel::token{{0, 0}, cel::close_delimiter{cel::delimiter::none}}};
    CHECK_THROWS_AS(parser.parse_tokens(invisible), cel::parse_error);
    auto host_close = tokens("1+2");
    host_close.back() = {{3, 4}, cel::close_delimiter{cel::delimiter::brace}};
    cel::token_cursor host(host_close);
    CHECK(parser.parse_expression(host).execute(stack, {}).as<double>() == 3);
    CHECK(std::holds_alternative<cel::close_delimiter>(host.current().value));
    CHECK_THROWS_AS(parser.parse_tokens(host_close), cel::parse_error);
    const auto terminal = std::array{
        cel::token{{0, 1}, cel::numeric_literal{1, "1", "", cel::numeric_form::integer}},
        cel::token{{1, 1}, cel::end_token{}}, cel::token{{2, 3}, cel::identifier{"ignored"}}};
    CHECK(parser.parse_tokens(terminal).execute(stack, {}).as<double>() == 1);
    const auto external_suffix = std::array{
        cel::token{{0, 4}, cel::numeric_literal{1, "1i32", "i32", cel::numeric_form::integer}}};
    CHECK_THROWS_AS(parser.parse_tokens(external_suffix), cel::parse_error);
}

TEST_CASE("recursive input has source resource diagnostics flat chains remain iterative") {
    cel::op_lookup lookup;
    lookup.register_op<double, double>("step", [](double x) { return x; });
    cel::parser parser{std::move(lookup)};
    CHECK(evaluate(parser, std::string(32, '(') + "1" + std::string(32, ')')).as<double>() == 1);
    CHECK(evaluate(parser, std::string(127, '(') + "1" + std::string(127, ')')).as<double>() == 1);
    const auto deep = std::string(1000, '(') + "1" + std::string(1000, ')');
    try {
        (void)parser.parse_str(deep);
        FAIL("deep input accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::resource_limit);
        CHECK(error.span().begin < 1000);
    }
    std::string chain = "1";
    for (int i = 0; i != 3000; ++i)
        chain += "+1";
    CHECK(evaluate(parser, chain).as<double>() == 3001);
    CHECK(evaluate(parser, std::string(3000, '!') + "false").as<bool>() == false);
    std::string array = "[1";
    for (int i = 0; i != 3000; ++i)
        array += ",1";
    array += "]";
    CHECK(evaluate(parser, array).size() == 3001);
    std::string postfix = "1";
    for (int i = 0; i != 3000; ++i)
        postfix += " as f64";
    CHECK(evaluate(parser, postfix).as<double>() == 1);
    std::string member = "1";
    for (int i = 0; i != 3000; ++i)
        member += ".step()";
    CHECK(evaluate(parser, member).as<double>() == 1);
    std::string logical = "false";
    for (int i = 0; i != 3000; ++i)
        logical += " || false";
    CHECK_FALSE(evaluate(parser, logical).as<bool>());
    std::string calls = "(|| || 1)";
    // Actual higher-order invocations exercise postfix without an artificial callback carrier.
    CHECK(evaluate(parser, calls + "()()").as<double>() == 1);
    std::string conditionals;
    for (int i = 0; i != 1000; ++i)
        conditionals += "if false {()} else ";
    conditionals += "{()}";
    CHECK_THROWS_AS(parser.parse_str(conditionals), cel::parse_error);
}
