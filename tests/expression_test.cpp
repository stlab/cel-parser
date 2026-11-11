// SPDX-License-Identifier: BSL-1.0
#include "compiler.hpp"
#include "lookup_access.hpp"
#include <cel/parser/compiler.hpp>
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
std::vector<cel::parser_detail::token> tokens(std::string_view source) {
    cel::parser_detail::lex_lexer lexer(source);
    std::vector<cel::parser_detail::token> result;
    do {
        result.push_back(lexer.current());
        if (std::holds_alternative<cel::parser_detail::end_token>(lexer.current().value)) break;
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
         std::array{std::pair{"1.0 + 2.0 * 3.0", 7.0}, std::pair{"10.0 - 3.0 - 2.0", 5.0},
                    std::pair{"20.0 / 2.0 / 2.0", 5.0}, std::pair{"-2.0 as f64 * 3.0", -6.0},
                    std::pair{"1.0 as f64 as f64 + 2.0", 3.0}, std::pair{"16.0 + 2e0", 18.0},
                    std::pair{"2.0f64 + 3.0", 5.0}, std::pair{"-5.0 % 2.0", -1.0}}) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).as<double>() == expected);
    }
    CHECK(evaluate(parser, "!false && 1.0 + 2.0 == 3.0 || false").as<bool>());
    CHECK(evaluate(parser, "true as bool == !false").as<bool>());
    for (auto text :
         {"1.0 < 2.0", "2.0 > 1.0", "1.0 <= 1.0", "1.0 >= 1.0", "1.0 != 2.0", "true != false"}) {
        CHECK(evaluate(parser, text).as<bool>());
    }
    CHECK(std::isinf(evaluate(parser, "1e9999").as<double>()));
    CHECK(evaluate(parser, "1e-9999").as<double>() == 0.0);
    CHECK(evaluate(parser, "5e-324").as<double>() == std::numeric_limits<double>::denorm_min());
    CHECK(std::signbit(evaluate(parser, "-0.0").as<double>()));
    CHECK(std::isnan(evaluate(parser, "0.0 / 0.0").as<double>()));
    for (auto text : {"1.0 < 2.0 < 3.0", "true == false == true", "+1.0", "true as f64",
                      "1.0 as bool", "1.0 as (f64)", "1.0f32", "1.0 | 2.0", "1.0 & 2.0",
                      "1.0 << 2.0", "1.0 ^ 2.0", "else", "as", "1.0 +"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("numeric values require floating literal forms without integer coercion") {
    cel::parser parser{cel::op_lookup{}};
    for (const auto& [text, span] : std::array{
             std::pair{"1", cel::source_span{0, 1}}, std::pair{"1f64", cel::source_span{0, 4}},
             std::pair{"1+2.0", cel::source_span{0, 1}}, std::pair{"1.0+1", cel::source_span{4, 5}},
             std::pair{"(|x:f64|x)(1)", cel::source_span{11, 12}},
             std::pair{"0x10", cel::source_span{0, 4}}, std::pair{"0b10", cel::source_span{0, 4}},
             std::pair{"0o10", cel::source_span{0, 4}}}) {
        CAPTURE(text);
        error_at(parser, text, cel::error_category::unsupported_literal, span);
        CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, tokens(text)),
                        cel::parse_error);
    }
    CHECK(evaluate(parser, "1.0+2.0").as<double>() == 3.0);
    CHECK(evaluate(parser, "(|x:f64|x)(1.0)").as<double>() == 1.0);
    CHECK(evaluate(parser, "(1.0,2.0).0x1").as<double>() == 2.0);
}

TEST_CASE("postfix brackets remain host input and are not core array indexing") {
    cel::parser parser{cel::op_lookup{}};
    const auto input = tokens("[1.0][0.0]");
    CHECK_THROWS_AS(parser.parse_str("[1.0][0.0]"), cel::parse_error);
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, input),
                    cel::parse_error);
    cel::parser_detail::token_cursor cursor(input);
    const auto program = cel::parser_detail::parser_access::parse_expression(parser, cursor);
    REQUIRE(std::holds_alternative<cel::parser_detail::open_delimiter>(cursor.current().value));
    CHECK(cursor.current().span == cel::source_span{5, 6});
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).at(0).as<double>() == 1.0);
}

TEST_CASE("core lookup does not supply standard-library functions") {
    cel::parser parser{cel::op_lookup{}};
    for (auto text : {"min(1.0,2.0)", "max(1.0,2.0)", "clamp(4.0,1.0,3.0)", "abs(-2.0)",
                      "signum(-2.0)", "sqrt(4.0)", "floor(2.5)", "ceil(2.5)", "trunc(-2.5)",
                      "fract(2.5)", "round_ties_even(2.5)", "(-2.0).abs()"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("tuple group unit array annotations and strict separators") {
    cel::parser parser{cel::op_lookup{}};
    CHECK(evaluate(parser, "()").size() == 0);
    CHECK(evaluate(parser, "(1.0 + 2.0)").as<double>() == 3);
    CHECK(evaluate(parser, "(1.0,)").at(0).as<double>() == 1);
    CHECK(evaluate(parser, "((), (true,), [[1.0, 2.0], [3.0]])").at(2).at(1).at(0).as<double>() ==
          3);
    CHECK(evaluate(parser, "[]: [f64]").size() == 0);
    CHECK(evaluate(parser, "[1.0, 2.0]: [f64]").at(1).as<double>() == 2);
    CHECK(evaluate(parser, "[[]: [bool]]").at(0).size() == 0);
    for (auto text :
         {"[]", "[1.0,]", "(1.0, 2.0,)", "(,)", "[,]", "[1.0 2.0]", "(1.0 2.0)", "[1.0]: f64",
          "[true]: [f64]", "[1.0, true]", "[:[f64]]", "[[]: [bool]]: [[f64]]", "([1.0)"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    error_at(parser, "[]:(f64)", cel::error_category::type_error, {3, 8});
    error_at(parser, "[]:((f64,[bool]))   ", cel::error_category::type_error, {3, 17});
    error_at(parser, "[true]:[f64]", cel::error_category::type_error, {0, 12});
    error_at(parser, "[true]:([f64])   ", cel::error_category::type_error, {0, 14});
    error_at(parser, "[[]:[bool]]:([[f64]])   ", cel::error_category::type_error, {0, 21});
    error_at(parser, "[1.0,true]:([f64])   ", cel::error_category::type_error, {0, 18});
}

TEST_CASE("tuple selectors use retained raw digits rather than double conversion") {
    cel::parser parser{cel::op_lookup{}};
    for (auto text : {"((1.0, 2.0),).0.1", "(1.0, 2.0).0x1", "(1.0, 2.0).0b1", "(1.0, 2.0).0o1",
                      "(1.0, 2.0).0_1", "((1.0, 2.0),).0_0.0_1"}) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).as<double>() == 2);
    }
    for (auto text :
         {"(1.0,).0f64", "(1.0,).0e0", "(1.0,).0.1e0", "(1.0,).1.", "(1.0,).184467440737095516160",
          "(1.0,).2", "1.0.0", "(1.0,).-1", "(1.0,)."}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    error_at(parser, "(true).0", cel::error_category::type_error, {0, 8});
    error_at(parser, "(1.0,).2", cel::error_category::type_error, {0, 8});
    error_at(parser, "((1.0,),).0.2", cel::error_category::type_error, {0, 13});
    error_at(parser, "(1.0,).0f64", cel::error_category::malformed_input, {7, 11});
    error_at(parser, "(1.0,).0e0", cel::error_category::malformed_input, {7, 10});
}

TEST_CASE("complete expressions reject every bracket postfix form") {
    cel::parser parser{cel::op_lookup{}};
    for (auto text : {"[[1.0, 2.0], [3.0, 4.0]][1.0][0.0]", "[4.0][-0.0]", "[1.0][true]", "[1.0][]",
                      "[1.0][0.0, 0.0]", "1.0[0.0]", "([]:[f64])[0.0]"}) {
        CAPTURE(text);
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
}

TEST_CASE("all six range forms preserve complete type and endpoint order") {
    cel::parser parser{cel::op_lookup{}};
    const auto forms = std::array{std::pair{"1.0 + 2.0..4.0 * 2.0", cel::range_kind::range},
                                  std::pair{"1.0..=3.0", cel::range_kind::range_inclusive},
                                  std::pair{"1.0..", cel::range_kind::range_from},
                                  std::pair{"..3.0", cel::range_kind::range_to},
                                  std::pair{"..=3.0", cel::range_kind::range_to_inclusive},
                                  std::pair{"..", cel::range_kind::range_full}};
    for (const auto& [text, kind] : forms) {
        CAPTURE(text);
        CHECK(evaluate(parser, text).type().form() == kind);
    }
    auto range = evaluate(parser, "1.0 + 2.0..4.0 * 2.0");
    CHECK(range.at(0).as<double>() == 3);
    CHECK(range.at(1).as<double>() == 8);
    for (auto text : {"..=", "1.0..=", "1.0..2.0..3.0", "true..false", "1.0==2.0..3.0==4.0"}) {
        CHECK_THROWS_AS(parser.parse_str(text), cel::parse_error);
    }
    for (auto text : {"..else", "1.0..else", "..as", "1.0..as"}) {
        const auto input = tokens(text);
        cel::parser_detail::token_cursor cursor(input);
        CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_expression(parser, cursor),
                        cel::parse_error);
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
    auto logical = parser.parse_str("hit(1.0) || explode()");
    CHECK(trace.empty());
    cel::raw_stack stack;
    stack.push(std::tuple{true, 99.0});
    CHECK(logical.execute(stack, {}).as<bool>());
    CHECK(trace == std::vector<int>{1});
    CHECK(evaluate(parser, "false && explode()").as<bool>() == false);
    CHECK(evaluate(parser, "if true { true } else { explode() }").as<bool>());
    CHECK(evaluate(parser, "if false { explode() } else if true { hit(2.0) } else { false }")
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
    for (auto text : {"if true { 1.0 }", "if true { 1.0 } else { false }", "false && 1.0",
                      "if 1.0 { () }", "if true {}", "if true {1.0} else {}",
                      "if true {1.0} else 2.0", "if true 1.0", "if true {1.0; 2.0}"}) {
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
    auto program = parser.parse_str("Pair { y: tap(2.0), x: tap(1.0) }.x.plus(3.0)");
    CHECK(trace.empty());
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).as<double>() == 4);
    CHECK(trace == std::vector<double>{2, 1});
    CHECK(evaluate(parser, "Pair {y: 2.0, x: 1.0}.y").as<double>() == 2);
    CHECK(evaluate(parser, "Empty {}").size() == 0);
    CHECK(evaluate(parser, "1.0.named()").as<double>() == 10);
    CHECK(evaluate(parser, "named(2.0)").as<double>() == 20);
    CHECK_THROWS_AS(parser.parse_str("1.0.named()(2.0)"), cel::parse_error);
    CHECK_THROWS_AS(parser.parse_str("plus(1.0, 2.0)"), cel::parse_error);
    error_at(parser, "Pair{x:true,y:2.0}", cel::error_category::type_error, {5, 6});
    error_at(parser, "Unknown{}", cel::error_category::type_error, {0, 9});
    for (auto text : {"Pair{x:1.0}", "Pair{x:1.0,x:2.0}", "Pair{z:1.0,y:2.0}", "Pair{x:true,y:2.0}",
                      "Pair{x:1.0,y:2.0,}", "Pair{x 1.0,y:2.0}", "Unknown{}", "named(1.0,)",
                      "1.0.plus(2.0,)", "Pair{x:1.0,y:2.0}.missing"}) {
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
    CHECK(evaluate(parser, "(|x:f64,y:bool| if y {x+1.0} else {0.0})(4.0,true)").as<double>() == 5);
    const auto nested =
        evaluate(parser, "(|x:((f64,bool),[f64])| (x.0.0,x.1))(((1.0,true),[2.0]))");
    CHECK(nested.at(0).as<double>() == 1);
    CHECK(nested.at(1).at(0).as<double>() == 2);
    CHECK(evaluate(parser, "(|| leaf())()").as<client>().number == 8);
    CHECK(evaluate(parser, "(|| [leaf()])()").at(0).as<client>().number == 8);
    CHECK(evaluate(parser, "[leaf()]:[Client]").at(0).as<client>().number == 8);
    CHECK(evaluate(parser, "[]:[Client]").size() == 0);
    CHECK(evaluate(parser, "(|| || 7.0)()()").as<double>() == 7);
    cel::raw_stack stack;
    const auto produced = evaluate(parser, "(|| [|| 7.0])()");
    CHECK(cel::callable::from_value(produced.at(0)).call(stack, {}).as<double>() == 7);
    CHECK(evaluate(parser, "(if true {||7.0} else {||8.0})()").as<double>() == 7);
    CHECK(evaluate(parser, "(|x:()|x)(())").size() == 0);
    const auto annotated = evaluate(parser, "[|| 7.0]:[Fn((),f64)]");
    CHECK(cel::callable::from_value(annotated.at(0)).call(stack, {}).as<double>() == 7);
    CHECK(evaluate(parser, "(Box{f:|| 9.0}.f)()").as<double>() == 9);
    CHECK(evaluate(parser, "Box{f:|| 9.0}.f()()").as<double>() == 9);
    CHECK(evaluate(parser, "[]:[Fn((),f64)]").size() == 0);
    CHECK(evaluate(parser, "[]:[([Fn((),f64)],bool)]").size() == 0);
    CHECK(evaluate(parser, "[]:[Box]").size() == 0);
    CHECK(evaluate(parser, "(|| []:[Fn((),f64)])()").size() == 0);
    CHECK(evaluate(parser, "[]:[Fn((),Fn((),f64))]").size() == 0);
    CHECK(evaluate(parser, "(|r:RangeFull|r)(..)").type().form() == cel::range_kind::range_full);
    for (auto text : {"|x:Client| 1.0", "|x:[Client]| 1.0", "|f:Fn((),f64)| 1.0", "|x:f64,x:f64| x",
                      "|if:f64| 1.0", "|else:f64| 1.0", "|as:f64| 1.0", "|true:f64| 1.0",
                      "|x:f64,| 1.0", "| | 1.0", "||", "|x f64| x", "(|x:f64| x)(true)",
                      "(|| 1.0)(2.0)", "[||1.0]:[Fn((),bool)]"}) {
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
    cel::parser_detail::lookup_access::push_scope(
        lookup, [](std::string_view name, cel::dyn_segment& b, std::size_t n, cel::source_span) {
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
    CHECK(evaluate(parser, "(|ambient:f64| ambient)(9.0)").as<double>() == 9);
    CHECK(evaluate(parser, "ambient").as<double>() == 3);
    CHECK(evaluate(parser, "if true {ambient} else {0.0}").as<double>() == 3);
    CHECK(evaluate(parser, "(|ambient:bool| if ambient {1.0} else {2.0})(true)").as<double>() == 1);
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
    CHECK(evaluate(parser, "Pair{x:1.0,y:2.0}.x").as<double>() == 2);
    CHECK(evaluate(parser, "(|p:Pair|p.x)(Pair{x:1.0,y:2.0})").as<double>() == 2);
    CHECK(evaluate(parser, "(2.0)(3.0)").as<double>() == 5);
    CHECK(evaluate(parser, "1.0 | 2.0 * 3.0").as<double>() == 7);
    CHECK(evaluate(parser, "[1.0]:[Alias]").at(0).as<double>() == 1);
    CHECK_THROWS_AS(parser.parse_str("1.0 as Alias"), cel::parse_error);
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
        [&active](cel::dyn_segment&, cel::source_span) { (void)active->parse_str("1.0"); });
    cel::parser parser(std::move(lookup));
    active = &parser;
    CHECK_THROWS_AS(parser.parse_str("1.0.fail()"), std::invalid_argument);
    CHECK_THROWS_AS(parser.parse_str("1.0.reenter()"), std::logic_error);
    CHECK_THROWS_AS(parser.parse_str("|| 1.0.fail()"), std::invalid_argument);
    CHECK(evaluate(parser, "1.0+2.0").as<double>() == 3);
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
        return parser.parse_str("2.0+3.0*4.0");
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
    auto input = tokens("1.0+2.0, host");
    cel::parser_detail::token_cursor cursor(input);
    auto program = cel::parser_detail::parser_access::parse_expression(parser, cursor);
    CHECK(std::get<cel::parser_detail::punctuation>(cursor.current().value).spelling() == ",");
    cel::raw_stack stack;
    CHECK(program.execute(stack, {}).as<double>() == 3);
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, input),
                    cel::parse_error);
    CHECK(cel::parser_detail::parser_access::parse_tokens(parser, tokens("(1.0,2.0).1"))
              .execute(stack, {})
              .as<double>() == 2);
    error_at(parser, "1.0 2.0", cel::error_category::malformed_input, {4, 7});
    error_at(parser, "[1.0,]", cel::error_category::malformed_input, {5, 6});
    error_at(parser, "(1.0]", cel::error_category::delimiter_error, {4, 5});
    error_at(parser, "missing", cel::error_category::lookup_error, {0, 7});
    error_at(parser, "[1.0,true]:[f64]   ", cel::error_category::type_error, {0, 16});
    for (const auto& [text, expected] :
         std::array{std::pair{"[]:(f64)   , host", cel::source_span{3, 8}},
                    std::pair{"[true]:([f64])   , host", cel::source_span{0, 14}},
                    std::pair{"[[]:[bool]]:([[f64]])   , host", cel::source_span{0, 21}}}) {
        CAPTURE(std::string_view(text));
        const auto annotated = tokens(text);
        cel::parser_detail::token_cursor prefix(annotated);
        try {
            (void)cel::parser_detail::parser_access::parse_expression(parser, prefix);
            FAIL("invalid annotation accepted");
        } catch (const cel::parse_error& error) {
            CHECK(error.category() == cel::error_category::type_error);
            CHECK(error.span() == expected);
        }
        CHECK(std::get<cel::parser_detail::punctuation>(prefix.current().value).spelling() == ",");
    }
    auto invalid = std::array{cel::parser_detail::token{
        {20, 21},
        cel::parse_error(cel::error_category::unsupported_literal, {20, 21}, "producer")}};
    try {
        (void)cel::parser_detail::parser_access::parse_tokens(parser, invalid);
        FAIL("producer diagnostic ignored");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::unsupported_literal);
        CHECK(error.span() == cel::source_span{20, 21});
        CHECK(std::string(error.what()) == "producer");
    }
    const auto malformed = std::array{
        cel::parser_detail::token{
            {10, 11},
            cel::parser_detail::open_delimiter{cel::parser_detail::delimiter::parenthesis}},
        cel::parser_detail::token{{11, 14},
                                  cel::parser_detail::numeric_literal{
                                      1, "1.0", "", cel::parser_detail::numeric_form::decimal}},
        cel::parser_detail::token{
            {14, 15}, cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::bracket}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, malformed),
                    cel::parse_error);
    const auto invisible = std::array{
        cel::parser_detail::token{
            {0, 0}, cel::parser_detail::open_delimiter{cel::parser_detail::delimiter::none}},
        cel::parser_detail::token{
            {0, 0}, cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::none}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, invisible),
                    cel::parse_error);
    auto host_close = tokens("1.0+2.0");
    host_close.back() = {{7, 8},
                         cel::parser_detail::close_delimiter{cel::parser_detail::delimiter::brace}};
    cel::parser_detail::token_cursor host(host_close);
    CHECK(cel::parser_detail::parser_access::parse_expression(parser, host)
              .execute(stack, {})
              .as<double>() == 3);
    CHECK(std::holds_alternative<cel::parser_detail::close_delimiter>(host.current().value));
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, host_close),
                    cel::parse_error);
    const auto terminal = std::array{
        cel::parser_detail::token{{0, 3},
                                  cel::parser_detail::numeric_literal{
                                      1, "1.0", "", cel::parser_detail::numeric_form::decimal}},
        cel::parser_detail::token{{3, 3}, cel::parser_detail::end_token{}},
        cel::parser_detail::token{{4, 5}, cel::parser_detail::identifier{"ignored"}}};
    CHECK(cel::parser_detail::parser_access::parse_tokens(parser, terminal)
              .execute(stack, {})
              .as<double>() == 1);
    const auto external_suffix = std::array{cel::parser_detail::token{
        {0, 4},
        cel::parser_detail::numeric_literal{1, "1i32", "i32",
                                            cel::parser_detail::numeric_form::integer}}};
    CHECK_THROWS_AS(cel::parser_detail::parser_access::parse_tokens(parser, external_suffix),
                    cel::parse_error);
}

TEST_CASE("recursive input has source resource diagnostics flat chains remain iterative") {
    cel::op_lookup lookup;
    lookup.register_op<double, double>("step", [](double x) { return x; });
    cel::parser parser{std::move(lookup)};
    CHECK(evaluate(parser, std::string(32, '(') + "1.0" + std::string(32, ')')).as<double>() == 1);
    CHECK(evaluate(parser, std::string(127, '(') + "1.0" + std::string(127, ')')).as<double>() ==
          1);
    const auto deep = std::string(1000, '(') + "1.0" + std::string(1000, ')');
    try {
        (void)parser.parse_str(deep);
        FAIL("deep input accepted");
    } catch (const cel::parse_error& error) {
        CHECK(error.category() == cel::error_category::resource_limit);
        CHECK(error.span().begin < 1000);
    }
    std::string chain = "1.0";
    for (int i = 0; i != 3000; ++i)
        chain += "+1.0";
    CHECK(evaluate(parser, chain).as<double>() == 3001);
    CHECK(evaluate(parser, std::string(3000, '!') + "false").as<bool>() == false);
    std::string array = "[1.0";
    for (int i = 0; i != 3000; ++i)
        array += ",1.0";
    array += "]";
    CHECK(evaluate(parser, array).size() == 3001);
    std::string postfix = "1.0";
    for (int i = 0; i != 3000; ++i)
        postfix += " as f64";
    CHECK(evaluate(parser, postfix).as<double>() == 1);
    std::string member = "1.0";
    for (int i = 0; i != 3000; ++i)
        member += ".step()";
    CHECK(evaluate(parser, member).as<double>() == 1);
    std::string logical = "false";
    for (int i = 0; i != 3000; ++i)
        logical += " || false";
    CHECK_FALSE(evaluate(parser, logical).as<bool>());
    std::string calls = "(|| || 1.0)";
    // Actual higher-order invocations exercise postfix without an artificial callback carrier.
    CHECK(evaluate(parser, calls + "()()").as<double>() == 1);
    std::string conditionals;
    for (int i = 0; i != 1000; ++i)
        conditionals += "if false {()} else ";
    conditionals += "{()}";
    CHECK_THROWS_AS(parser.parse_str(conditionals), cel::parse_error);
}
