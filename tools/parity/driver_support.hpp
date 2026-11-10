// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARITY_DRIVER_SUPPORT_HPP
#define CEL_PARITY_DRIVER_SUPPORT_HPP
#include <cel/parser/lookup.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cel::parity {
/// Identifies a deliberate selected callback failure.
struct callback_error : std::runtime_error {
    /// Constructs the distinct client-owned selected-failure error.
    callback_error() : std::runtime_error("selected callback") {}
};
/// Borrows one decoded expression and its explicit registration selection.
struct request {
    std::string_view setup;  ///< Selects explicitly configured public registrations.
    std::string_view source; ///< Borrows one decoded expression for compilation.
};
/// Produces exact binary64 bits, normalizing only unspecified NaN payloads.
std::string bits(double number);
/// Serializes an execution trace with the same explicit IEEE normalization as values.
/// Complexity: O(trace length) time and response space.
std::string trace_text(const std::vector<double>& trace);
/// Executes one configured request after destroying its compiler.
/// Complexity: O(compilation + three executions + serialization).
void run(request input);

/// Executes and observes three cached calls with identical normalized types, values, and traces.
/// Execute returns an owning public value; observe and prefix check it and caller state.
/// Only source-defined execution errors under their selected setups produce runtime records.
/// Observation, prefix, unrelated execution errors, and changing successful observations propagate.
/// Complexity: O(three executions, observations, and trace serialization).
template <class Execute, class Observe, class Prefix>
std::string evaluate_cached(std::string_view setup,
                            std::vector<double>& trace,
                            Execute execute,
                            Observe observe,
                            Prefix prefix) {
    std::string response;
    for (int repeat = 0; repeat != 3; ++repeat) {
        trace.clear();
        std::optional<decltype(execute())> object;
        try {
            object.emplace(execute());
        } catch (const callback_error&) {
            if (setup != "callback_error") throw;
            return "runtime\tcallback\t" + trace_text(trace);
        } catch (const std::invalid_argument& error) {
            if (setup != "clamp_error" || std::string_view(error.what()) != "invalid clamp bounds")
                throw;
            return "runtime\tclamp\t" + trace_text(trace);
        } catch (const std::out_of_range& error) {
            const std::string_view witness(error.what());
            if (setup != "index_error" || (witness != "invalid floating array index" &&
                                           witness != "array index out of bounds"))
                throw;
            return "runtime\tindex\t" + trace_text(trace);
        }
        const auto next = observe(*object) + '\t' + trace_text(trace);
        if (repeat && response != next) throw std::logic_error("cached result or trace changed");
        response = next;
        prefix();
    }
    return response;
}
} // namespace cel::parity
#endif
