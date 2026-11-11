// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>

/// Compiles a scalar expression and reuses a caller-owned stack.
int main() try {
    cel::parser compiler(cel::op_lookup{});
    auto program = compiler.parse_str("1.0f64").prepare<double>();
    cel::raw_stack stack;
    return program.call(stack) == 1.0 ? 0 : 1;
} catch (const std::exception&) {
    return 1;
}
