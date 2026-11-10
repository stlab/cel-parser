// SPDX-License-Identifier: BSL-1.0
#include <array>
#include <cel/parser/lookup.hpp>
#include <tuple>
#include <vector>

/// Executes cached owned callbacks and complete composites through installed packages only.
int main() try {
    cel::dyn_segment builder;
    {
        cel::op_lookup lookup;
        const double offset = 3.0;
        lookup.register_op<double, double>("offset", [offset](double x) { return x + offset; });
        builder.push_argument(0, cel::value_type::leaf<double>());
        lookup.apply("offset", builder, 1, {0, 6});
    }
    auto prepared = std::move(builder).finish().prepare<double, double>();
    cel::raw_stack stack;
    if (prepared.call(stack, 4.0) != 7.0 || prepared.call(stack, 5.0) != 8.0) return 1;
    cel::op_lookup lookup;
    cel::install_standard_library(lookup);
    cel::dyn_segment named;
    lookup.apply("abs", named, 0, {});
    named.just(cel::value::from(-4.0));
    lookup.apply("()", named, 2, {});
    if (std::move(named).finish().prepare<double>().call(stack) != 4.0) return 2;
    using nested = std::tuple<bool, std::vector<std::tuple<double, std::vector<bool>>>>;
    const nested input{true, {{2.0, {true, false}}}};
    lookup.register_op<nested, nested>("nested", [](nested x) { return x; });
    cel::dyn_segment aggregate;
    aggregate.just(cel::value::from(input));
    lookup.apply("nested", aggregate, 1, {});
    if (std::move(aggregate).finish().prepare<nested>().call(stack) != input) return 3;
    std::vector<cel::token> tokens;
    cel::lex_lexer lexer("Fn(([f64],), bool)");
    while (!std::holds_alternative<cel::end_token>(lexer.current().value)) {
        tokens.push_back(lexer.current());
        lexer.advance();
    }
    if (cel::parse_type(tokens, lookup).parameters().size() != 1) return 4;
    cel::lex_lexer inline_lexer("Fn(([f64],), bool), next");
    if (cel::parse_type(inline_lexer, lookup).parameters().size() != 1 ||
        std::get<cel::punctuation>(inline_lexer.current().value).spelling() != ",")
        return 6;
    return 0;
} catch (...) {
    return 5;
}
