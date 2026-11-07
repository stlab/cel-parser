// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_EXAMPLE_CACHED_DECODED_HPP
#define CEL_EXAMPLE_CACHED_DECODED_HPP
#include <cel/parser.hpp>

/// Demonstrates a client-owned cache; JSON decoding is outside this interface.
namespace client {
/// Compiles decoded expressions with scalar f64 results and destroys the compiler.
/// Throws parse_error for malformed input and invalid_argument for non-f64 results.
/// Returns independent prepared handles retaining their code and captures.
/// Complexity: O(total compilation and preparation work), plus O(expression count) handles.
inline std::vector<cel::prepared_segment<double>> compile_decoded(
    std::span<const std::string_view> expressions) {
    cel::op_lookup lookup;
    cel::install_standard_library(lookup);
    cel::parser compiler(std::move(lookup));
    std::vector<cel::prepared_segment<double>> result;
    result.reserve(expressions.size());
    // Prepare each expression before the compiler and decoded strings cease to exist.
    for (const auto source : expressions)
        result.push_back(compiler.parse_str(source).prepare<double>());
    return result;
}
/// Reserves the maximum known requirements on one caller-owned prefixed stack.
/// Call before entering execution, without intervening prefix mutation.
/// Nested arbitrary callback work and composite payload costs remain separate.
/// Throws on allocation or checked extent overflow.
/// Complexity: O(program count).
inline void reserve(std::span<const cel::prepared_segment<double>> programs,
                    cel::raw_stack& stack) {
    // Each reservation retains existing byte alignment and capacity.
    for (const auto& program : programs) {
        const auto needed = program.requirements().for_prefix(stack.cursor(), stack.depth());
        stack.reserve(needed.bytes.size, needed.bytes.alignment);
        stack.reserve_depth(needed.slots);
    }
}
} // namespace client
#endif
