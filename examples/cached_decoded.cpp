// SPDX-License-Identifier: BSL-1.0
#include "cached_decoded.hpp"
#include <iostream>

/// Evaluates multiple already-decoded strings repeatedly on one reserved caller stack.
/// JSON transport/decoding remains the application's responsibility.
int main() try {
    const auto programs = client::compile_decoded(
        std::array<std::string_view, 3>{"2f64+3f64*4f64", "sqrt(81f64)", "(|| 7f64)()"});
    cel::raw_stack stack;
    stack.push(true);
    client::reserve(programs, stack);
    double sum = 0;
    for (int repeat = 0; repeat != 1000; ++repeat)
        for (const auto& program : programs)
            sum += program.call(stack);
    std::cout << "cached sum=" << sum << '\n';
    return sum == 30000 && stack.pop<bool>() ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
