// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_COMPILER_HPP
#define CEL_PARSER_COMPILER_HPP
#include <cel/parser/lookup.hpp>

namespace cel {
/// Owns lookup state and compiles current-token CEL expressions into reusable checked code.
/// Compiled programs retain values, descriptors, and callback captures after parser destruction.
/// Referenced client captures remain client-owned. Access is exclusive, including during emitters;
/// recursive entry throws logic_error. Failed parsing retires temporary scopes; subsequent parses
/// may reuse the parser. A selected client emitter's own failure leaves its lookup state subject
/// to that emitter's contract. The parser never executes client operations while compiling.
/// Example: `parser p(op_lookup{}); auto code = p.parse_str("1 + 2");`.
class parser {
    op_lookup lookup_;
    bool active_ = false;

public:
    /// Transfers the lookup's names, schemas, registrations, and scopes into this compiler.
    /// Example: `parser p(std::move(lookup));`.
    explicit parser(op_lookup lookup) : lookup_(std::move(lookup)) {}
    /// Prevents copying mutable lookup and compilation ownership.
    parser(const parser&) = delete;
    /// Prevents replacing lookup state through copied assignment.
    parser& operator=(const parser&) = delete;
    /// Compiles one complete decoded UTF-8 CEL source directly through its owned lexer.
    /// Rejects trailing input, unsupported/malformed syntax, incompatible types, and recursive
    /// nesting exceeding 128 active expression/conditional productions with owned parse_error
    /// diagnostics. Producer errors retain their category, message, and byte span.
    /// Client-emitter exceptions and allocation failures propagate without relabeling.
    /// Complexity: O(lexed bytes + emitted operations + lookup/type/structural metadata work)
    /// time, O(source bytes + code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `auto code = p.parse_str("(|| 7)()");`.
    compiled_segment parse_str(std::string_view source);
    /// Compiles one complete external flattened stream with the same productions and errors.
    /// Terminal EOF/error follows token_cursor's contract: later supplied tokens are ignored.
    /// Validates consumed visible delimiter pairs; invisible groups are unsupported syntax.
    /// Tokens/source need not survive successful compilation. No complete token copy is made.
    /// Complexity: O(consumed tokens + emitted operations + lookup/type/structural metadata work)
    /// time, O(code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `auto code = p.parse_tokens(tokens);`.
    compiled_segment parse_tokens(std::span<const token> tokens);
    /// Compiles one expression prefix and leaves its following host token unconsumed.
    /// A host close delimiter may follow a complete prefix; this method does not consume or
    /// validate a surrounding host group. Groups opened by CEL must close with their own kind.
    /// Failed parsing may advance the borrowed cursor; it does not rewind or speculate.
    /// Follows parse_tokens' type, producer, ownership, exclusivity, and resource contracts.
    /// Complexity: O(consumed tokens + emitted operations + lookup/type/structural metadata work)
    /// time, O(code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `token_cursor c(tokens); auto code = p.parse_expression(c);`.
    compiled_segment parse_expression(token_cursor& cursor);
};
} // namespace cel
#endif
