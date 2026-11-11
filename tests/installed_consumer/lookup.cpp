// SPDX-License-Identifier: BSL-1.0
#include <array>
#include <cel/parser.hpp>
#include <tuple>
#include <vector>

/// Identifies the client's named identity operation.
struct function_marker {};

/// Executes cached owned callbacks and complete composites through installed packages only.
int main() try {
    auto program = [] {
        cel::op_lookup lookup;
        const double offset = 3.0;
        lookup.register_op<double, double>("offset", [offset](double x) { return x + offset; });
        cel::parser compiler(std::move(lookup));
        return compiler.parse_str("4.0.offset()");
    }();
    auto prepared = program.prepare<double>();
    cel::raw_stack stack;
    if (prepared.call(stack) != 7.0 || prepared.call(stack) != 7.0) return 1;
    cel::op_lookup lookup;
    lookup.register_function("identity", function_marker{});
    lookup.register_op<double, double>("identity", [](double x) { return x; });
    using nested = std::tuple<bool, std::vector<std::tuple<double, std::vector<bool>>>>;
    const nested input{true, {{2.0, {true, false}}}};
    lookup.register_op<nested, nested>("nested", [](nested x) { return x; });
    cel::parser compiler(std::move(lookup));
    if (compiler.parse_str("identity(4.0)").prepare<double>().call(stack) != 4.0) return 2;
    if (compiler.parse_str("(true, [(2.0, [true, false])]).nested()")
            .prepare<nested>()
            .call(stack) != input)
        return 3;
    auto callable = compiler.parse_str("|x:[f64]| true").execute(stack, {});
    if (callable.type().parameters().size() != 1) return 4;
    return 0;
} catch (...) {
    return 5;
}
