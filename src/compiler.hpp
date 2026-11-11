// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_INTERNAL_COMPILER_HPP
#define CEL_PARSER_INTERNAL_COMPILER_HPP
#include "lexer.hpp"
#include <cel/parser/compiler.hpp>

namespace cel::parser_detail {
/// Compiles internal producer streams under the parser's ownership and exclusivity contracts.
struct parser_access {
    /// Compiles a complete borrowed stream; terminal producer tokens end traversal.
    /// Complexity: O(tokens + emitted code + lookup and type metadata).
    static compiled_segment parse_tokens(parser& compiler, std::span<const token> tokens);
    /// Compiles a prefix, leaving the following host token unconsumed.
    /// Complexity: O(consumed tokens + emitted code + lookup and type metadata).
    static compiled_segment parse_expression(parser& compiler, token_cursor& cursor);
};
} // namespace cel::parser_detail
#endif
