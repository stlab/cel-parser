// SPDX-License-Identifier: BSL-1.0
#include "../tools/parity/driver_support.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
using namespace cel::parity;

TEST_CASE("unrelated execution exceptions fail instead of matching selected error fixtures") {
    CHECK_THROWS_AS(run({"clamp_error", "unexpected_argument"}), std::invalid_argument);
    CHECK_THROWS_AS(run({"index_error", "unexpected_range"}), std::out_of_range);
}

TEST_CASE("cached executions reject a differing first or second trace with a stable value") {
    CHECK_THROWS_AS(run({"unstable_trace_first", "traceprobe"}), std::logic_error);
    CHECK_THROWS_AS(run({"unstable_trace_second", "traceprobe"}), std::logic_error);
    CHECK_NOTHROW(run({"std", "tap(1f64)"}));
}

TEST_CASE("serialization and prefix exceptions never become execution-error responses") {
    std::vector<double> trace;
    const auto execute = [] { return cel::value::from(1.0); };
    const auto observe = [](const cel::value&) { return std::string("ok\tD\t3ff0000000000000"); };
    const auto prefix = [] {};
    CHECK_THROWS_AS(evaluate_cached(
                        "clamp_error", trace, execute,
                        [](const cel::value&) -> std::string {
                            throw std::invalid_argument("invalid clamp bounds");
                        },
                        prefix),
                    std::invalid_argument);
    CHECK_THROWS_AS(evaluate_cached("index_error", trace, execute, observe,
                                    [] { throw std::out_of_range("array index out of bounds"); }),
                    std::out_of_range);
}

TEST_CASE("only source-defined selected execution witnesses normalize") {
    std::vector<double> trace;
    const auto observe = [](const cel::value&) { return std::string("unused"); };
    const auto prefix = [] {};
    CHECK(evaluate_cached(
              "clamp_error", trace,
              []() -> cel::value { throw std::invalid_argument("invalid clamp bounds"); }, observe,
              prefix) == "runtime\tclamp\t");
    for (const auto witness : {"invalid floating array index", "array index out of bounds"}) {
        CHECK(evaluate_cached(
                  "index_error", trace,
                  [witness]() -> cel::value { throw std::out_of_range(witness); }, observe,
                  prefix) == "runtime\tindex\t");
    }
    CHECK_THROWS_AS(evaluate_cached(
                        "std", trace,
                        []() -> cel::value { throw std::invalid_argument("invalid clamp bounds"); },
                        observe, prefix),
                    std::invalid_argument);
}
