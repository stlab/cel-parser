// SPDX-License-Identifier: BSL-1.0
#include <cel/parser/all.hpp>
#include <memory>

/// Compiles and receives nested callable/composite results with only installed public headers.
/// Exercises retained captures, explicit arguments, empty typed results, and caller-prefix reuse.
int main() try {
    auto program = [] {
        cel::op_lookup lookup;
        cel::install_standard_library(lookup);
        lookup.register_record(
            "Callbacks", std::array{cel::record_field{
                             "f", cel::value_type::callable({}, cel::value_type::leaf<double>())}});
        lookup.register_op<double, double>(
            "offset", [n = std::make_unique<double>(3)](double x) { return x + *n; });
        cel::parser compiler(std::move(lookup));
        return compiler.parse_str("(Callbacks{f:|| (-4).abs().offset()}, "
                                  "[]:[Fn((),Fn((),f64))], |x:[f64]| || 7)");
    }();
    cel::raw_stack stack;
    stack.push(99.0);
    for (int repeat = 0; repeat != 3; ++repeat) {
        auto result = program.execute(stack, {});
        auto fn = cel::callable::from_value(result.at(0).field("f"));
        if (fn.call(stack, {}).as<double>() != 7) return 1;
        if (result.at(1).size() != 0) return 2;
        auto outer = cel::callable::from_value(result.at(2));
        const std::array arguments{cel::value::from(std::vector{1.0, 2.0})};
        auto inner = cel::callable::from_value(outer.call(stack, arguments));
        if (inner.call(stack, {}).as<double>() != 7 || stack.depth() != 1) return 3;
    }
    return stack.pop<double>() == 99 ? 0 : 4;
} catch (...) {
    return 5;
}
