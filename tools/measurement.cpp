// SPDX-License-Identifier: BSL-1.0
#include "../examples/cached_decoded.hpp"
#include <cel/runtime/callable.hpp>
#include <charconv>
#include <chrono>
#include <iostream>

namespace {
/// Times a warmed workload with an observable result sink.
/// Throws if the workload's contract result changes; no timing threshold is enforced.
/// Complexity: O(iterations * workload cost).
template <class F>
void measure(std::string_view name, std::size_t iterations, double expected, F execute) {
    for (int i = 0; i != 100; ++i)
        if (execute() != expected) throw std::logic_error("warm result mismatch");
    double timed_sum = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i != iterations; ++i)
        timed_sum += execute();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - begin)
                             .count();
    if (timed_sum != expected * static_cast<double>(iterations))
        throw std::logic_error("measured result mismatch");
    std::cout << name << ',' << iterations << ',' << elapsed << ',' << timed_sum << '\n';
}
} // namespace

/// Measures cached scalar, owned composite, boxed, and dynamic callable costs independently.
/// The optional argument is a positive iteration count; default is 100000.
/// Complexity: O(total compilation + iterations * workload cost).
int main(int argc, char** argv) try {
    std::size_t iterations = 100000;
    if (argc == 2) {
        const std::string_view number(argv[1]);
        const auto parsed =
            std::from_chars(number.data(), number.data() + number.size(), iterations);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !iterations)
            throw std::invalid_argument("iteration count must be a positive integer");
    } else if (argc != 1) {
        throw std::invalid_argument("usage: measurement [ITERATIONS]");
    }
    const auto scalars = client::compile_decoded(
        std::array<std::string_view, 2>{"2+3*4+(81).sqrt()", "2+3*4+sqrt(81)"});
    const auto composite = [] {
        cel::parser compiler(cel::op_lookup{});
        return compiler.parse_str("[[1,2],[3,4]]");
    }();
    const auto typed_composite = composite.prepare<std::vector<std::vector<double>>>();
    const auto factory = [] {
        cel::parser compiler(cel::op_lookup{});
        return compiler.parse_str("|x:f64|x*2+1");
    }();
    cel::raw_stack stack;
    stack.push(true);
    stack.reserve(4096, 64);
    stack.reserve_depth(256);
    const auto fn = cel::callable::from_value(factory.execute(stack, {}));
    const std::array arguments{cel::value::from(2.0)};
    std::cout << "workload,iterations,nanoseconds,result_sum\n";
    measure("prepared-scalar", iterations, 23, [&] { return scalars[0].call(stack); });
    measure("named-scalar-aggregate-retirement", iterations, 23,
            [&] { return scalars[1].call(stack); });
    measure("prepared-owning-nested-array", iterations, 4,
            [&] { return typed_composite.call(stack)[1][1]; });
    measure("erased-owning-nested-array", iterations, 2,
            [&] { return static_cast<double>(composite.execute(stack, {}).size()); });
    measure("dynamic-callable", iterations, 5,
            [&] { return fn.call(stack, arguments).as<double>(); });
    if (stack.depth() != 1 || !stack.pop<bool>())
        throw std::logic_error("measurement lost the caller prefix");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
