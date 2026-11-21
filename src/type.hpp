// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_TYPE_HPP
#define CEL_PARSER_TYPE_HPP
#include "lexer.hpp"
#include <cel/parser/lookup.hpp>

namespace cel::parser_detail {
/// type_expression = identifier [ "(" [ type_expression { "," type_expression } ] ")" ]
/// | "[" type_expression "]" | "(" [ type_expression [ "," [ type_expression
/// { "," type_expression } ] ] ] ")".
/// Parses one current-token type and leaves the following host token unconsumed.
/// Each choice uses only the current token. A consumed comma requires another item except
/// for the required singleton-tuple comma; optional trailing commas are invalid.
/// Fn requires a parameter tuple and a result. Ordinary aggregates admit registered leaves.
/// Callable parameters admit recursively eligible bool/f64 composites, not client leaves
/// or nested callables; results admit any complete type. A callable type does not own code.
/// Returns an owned descriptor that survives the cursor, token source, and lookup.
/// Publishes the consumed byte interval in parsed_span, excluding surrounding whitespace
/// and the following host token. External tokens supply these offsets without re-lexing.
/// Leaves parsed_span unchanged on failure.
/// Throws source-located parse_error(malformed_input) for an expected type production,
/// delimiter_error for a missing/mismatched close, type_error for unsupported names or
/// ineligible callable types, and resource_limit beyond 128 active type productions.
/// Producer errors retain their category, message, and span. Allocation/accounting failures
/// propagate. Failure may advance the cursor; parsing never rewinds.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Parse a prefix with an owned type and leave its host separator:
/// \code{.cpp}
/// cel::op_lookup lookup;
/// cel::lex_lexer cursor("(f64,), host");
/// cel::source_span parsed;
/// auto type = cel::parse_type(cursor, lookup, parsed);
/// auto count = type.fields().size(); // 1; parsed is {0, 6}.
/// auto comma = std::get<cel::punctuation>(cursor.current().value).spelling(); // ","
/// \endcode
value_type parse_type(token_cursor& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one text type prefix and returns its exact consumed interval without a token copy.
/// Follows the token span-output overload's grammar, errors, ownership, and complexity.
/// Example: lex_lexer c("[f64] , next"); source_span s; auto t = parse_type(c, lookup, s).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup, source_span& parsed_span);
} // namespace cel::parser_detail
#endif
