// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_DIAGNOSTIC_HPP
#define CEL_PARSER_DIAGNOSTIC_HPP
#include <cstddef>
#include <stdexcept>
#include <string>

/// Provides checked CEL compilation, extension libraries, and owned results.
namespace cel {
/// \addtogroup parser_diagnostics
/// @{

/// Identifies a half-open UTF-8 byte interval; begin never exceeds end.
/// Example: source_span{0, 4} covers all bytes of "true".
struct source_span {
    std::size_t begin = 0; ///< Inclusive byte offset in the producer's source.
    std::size_t end = 0;   ///< Exclusive byte offset; equal to begin for an empty interval.

    /// Returns true if both byte offsets are equal.
    bool operator==(const source_span&) const = default;
};

/// Classifies a source diagnostic independently of its human-readable message.
/// Example: error_category::unsupported_literal identifies an f32 suffix.
enum class error_category {
    malformed_input,     ///< Invalid UTF-8, spelling, comment, or input character.
    unsupported_literal, ///< A literal or suffix outside bool/binary64 scope.
    type_error,          ///< An unknown, unsupported, ineligible, or incompatible type.
    lookup_error,        ///< An undefined name or unmatched operation signature.
    delimiter_error,     ///< An unmatched, mismatched, or unclosed delimiter.
    resource_limit       ///< A documented producer or compilation resource limit.
};

/// Owns a source-located diagnostic; copied diagnostics own independent messages.
/// Example: parse_error(error_category::malformed_input, {0, 1}, "invalid input").
class parse_error : public std::runtime_error {
    error_category category_;
    source_span span_;

public:
    /// Constructs a diagnostic with an owned message, category, and byte interval.
    /// Precondition: span.begin <= span.end.
    /// Complexity: O(message.size()) time and space.
    parse_error(error_category category, source_span span, std::string message);
    /// Returns the category supplied at construction.
    [[nodiscard]] error_category category() const noexcept { return category_; }
    /// Returns the byte interval supplied at construction, without translating offsets.
    [[nodiscard]] source_span span() const noexcept { return span_; }
};

/// @}
} // namespace cel
#endif
