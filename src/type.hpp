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
value_type parse_type(token_cursor& cursor, const op_lookup& lookup);
/// Parses one type directly from the owned text lexer, following the inline grammar contract.
/// Leaves the following host token unconsumed without materializing a separate token array.
/// Throws the source diagnostics and resource errors documented by the token-cursor overload.
/// Complexity: O(lexer traversal + name bytes + complete descriptor construction) time,
/// O(nodes + 128) additional storage, excluding the lexer's owned source and payload.
/// Example: lex_lexer cursor("[f64], next"); auto type = parse_type(cursor, lookup).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup);
/// Parses one token type prefix and returns its exact consumed byte interval in parsed_span.
/// Follows the two-argument prefix overload's grammar, errors, ownership, and complexity.
/// Postcondition: parsed_span runs from the first consumed token's begin to the last one's end,
/// excluding surrounding whitespace and the following host token.
/// External tokens supply these offsets; parsing does not re-lex their source.
/// Leaves parsed_span unchanged if parsing fails; the cursor may have advanced.
/// Example: source_span span; auto type = parse_type(cursor, lookup, span).
value_type parse_type(token_cursor& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one text type prefix and returns its exact consumed interval without a token copy.
/// Follows the token span-output overload's grammar, errors, ownership, and complexity.
/// Example: lex_lexer c("[f64] , next"); source_span s; auto t = parse_type(c, lookup, s).
value_type parse_type(lex_lexer& cursor, const op_lookup& lookup, source_span& parsed_span);
/// Parses one complete type and rejects trailing input; follows the inline grammar contract.
/// Throws parse_error(malformed_input) for input after the type and before terminal end.
/// Explicit end/error tokens follow token_cursor's contract; later tokens are ignored.
/// Returns an owned descriptor; tokens and lookup need not survive the call.
/// Complexity: O(tokens + name bytes + complete descriptor construction), O(nodes + 128) storage.
/// Example: auto type = parse_type(std::span<const token>(tokens), lookup).
value_type parse_type(std::span<const token> tokens, const op_lookup& lookup);
} // namespace cel::parser_detail
#endif
