// SPDX-License-Identifier: BSL-1.0
#include "../tools/parity/driver_support.hpp"
#include <cel/parser/compiler.hpp>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
using namespace cel::parity;

TEST_CASE("unrelated execution exceptions fail instead of matching selected error fixtures") {
    CHECK_THROWS_AS(run({"client", "unexpected_argument"}), std::invalid_argument);
    CHECK_THROWS_AS(run({"client", "unexpected_range"}), std::out_of_range);
}

TEST_CASE("core lookup has only intrinsics before any client registrations") {
    std::vector<double> trace;
    cel::parser core(lookup("core", trace));
    CHECK_NOTHROW(core.parse_str("1.0f64+2.0f64"));
    for (const auto source : {"sqrt(4.0f64)", "tap(1.0f64)", "Pair{x:1.0f64,y:2.0f64}"})
        CHECK_THROWS_AS(core.parse_str(source), cel::parse_error);
    cel::parser client(lookup("client", trace));
    CHECK_NOTHROW(client.parse_str("tap(1.0f64)"));
}

TEST_CASE("cached executions reject a differing first or second trace with a stable value") {
    CHECK_THROWS_AS(run({"unstable_trace_first", "traceprobe"}), std::logic_error);
    CHECK_THROWS_AS(run({"unstable_trace_second", "traceprobe"}), std::logic_error);
    CHECK_NOTHROW(run({"client", "tap(1.0f64)"}));
}

TEST_CASE("serialization and prefix exceptions never become execution-error responses") {
    std::vector<double> trace;
    const auto execute = [] { return cel::value::from(1.0); };
    const auto observe = [](const cel::value&) { return std::string("ok\tD\t3ff0000000000000"); };
    const auto prefix = [] {};
    CHECK_THROWS_AS(evaluate_cached(
                        "callback_error", trace, execute,
                        [](const cel::value&) -> std::string {
                            throw std::invalid_argument("invalid clamp bounds");
                        },
                        prefix),
                    std::invalid_argument);
    CHECK_THROWS_AS(evaluate_cached("callback_error", trace, execute, observe,
                                    [] { throw std::out_of_range("array index out of bounds"); }),
                    std::out_of_range);
}

TEST_CASE("only source-defined selected execution witnesses normalize") {
    std::vector<double> trace;
    const auto observe = [](const cel::value&) { return std::string("unused"); };
    const auto prefix = [] {};
    CHECK(evaluate_cached(
              "callback_error", trace, []() -> cel::value { throw callback_error{}; }, observe,
              prefix) == "runtime\tcallback\t");
    CHECK_THROWS_AS(
        evaluate_cached(
            "client", trace, []() -> cel::value { throw callback_error{}; }, observe, prefix),
        callback_error);
}
