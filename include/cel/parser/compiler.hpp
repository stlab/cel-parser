// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_COMPILER_HPP
#define CEL_PARSER_COMPILER_HPP
#include <cel/parser/lookup.hpp>

/// \file
/// Provides CEL expression compilation for clients with an owned operation registry.
/// See `<cel/parser/lookup.hpp>` for registrations and standalone type parsing.

namespace cel {
/// Owns lookup state and compiles CEL expressions into reusable checked programs.
/// Each successful call returns a program owning its values, descriptors, and emitted runtime
/// callbacks; the source, external tokens, and parser need not survive the program.
/// References inside client captures remain client-owned and must outlive their uses.
/// Custom scope callbacks and emitters follow their own code-retention contracts.
/// Precondition: clients provide exclusive access across threads, including during emitters.
/// Recursive entry throws std::logic_error before changing cursor or lookup state.
/// Failed parsing retires temporary scopes; subsequent parses may reuse the parser.
/// A selected client emitter's own failure leaves its lookup state subject to its contract.
/// Compilation invokes lookup scopes and emitters, not the emitted runtime operations.
/// Copying and moving are unavailable.
/// Compile and execute a program independently of its parser:
/// \code{.cpp}
/// auto code = [] {
///     cel::parser compiler(cel::op_lookup{});
///     return compiler.parse_str("1 + 2");
/// }();
/// cel::raw_stack stack;
/// double result = code.prepare<double>().call(stack); // 3.0
/// \endcode
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
    /// Compiles one complete decoded UTF-8 CEL source through an owned source copy.
    /// Selects grammar alternatives from the current token alone. Consuming a comma requires
    /// another item except for a required singleton-tuple comma; optional trailing commas fail.
    /// Returns an owned program under this class's ownership and failure contracts.
    /// Throws source-located parse_error for malformed_input, unsupported_literal, type_error,
    /// lookup_error, delimiter_error, or resource_limit as classified by error_category.
    /// Trailing input is malformed_input. More than 128 simultaneously active expression and
    /// conditional productions is resource_limit; iterative flat chains do not consume this
    /// nesting allowance. Type annotations have the separate parse_type production limit.
    /// Producer errors retain their category, message, and byte span.
    /// Recursive entry throws std::logic_error as documented by this class.
    /// Client-emitter exceptions and allocation failures propagate without relabeling.
    /// Complexity: O(lexer traversal + emitted operations + lookup/type/structural metadata work)
    /// time, O(source bytes + code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `auto code = p.parse_str("(|| 7)()");`.
    compiled_segment parse_str(std::string_view source);
    /// Compiles one complete external flattened stream with the same productions and errors.
    /// Borrows tokens unchanged for this call only.
    /// Follows parse_str's grammar and limit contracts.
    /// Terminal EOF/error follows token_cursor's contract: later supplied tokens are ignored.
    /// Validates consumed visible delimiter pairs; invisible groups are unsupported syntax.
    /// Returns an owned program; tokens/source need not survive successful compilation.
    /// No complete token copy is made.
    /// Complexity: O(consumed tokens + emitted operations + lookup/type/structural metadata work)
    /// time, O(code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `auto code = p.parse_tokens(tokens);`.
    compiled_segment parse_tokens(std::span<const token> tokens);
    /// Compiles one expression prefix and leaves its following host token unconsumed.
    /// A host close delimiter may follow a complete prefix; this method does not consume or
    /// validate a surrounding host group. Groups opened by CEL must close with their own kind.
    /// Failed parsing may advance the borrowed cursor; it does not rewind or speculate.
    /// Borrows cursor for this call. Follows parse_tokens' type, producer, ownership,
    /// exclusivity, and resource contracts, but does not require terminal end.
    /// Complexity: O(consumed tokens + emitted operations + lookup/type/structural metadata work)
    /// time, O(code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `token_cursor c(tokens); auto code = p.parse_expression(c);`.
    compiled_segment parse_expression(token_cursor& cursor);
};
// [knowledge] Preserve current-token LL(1) compilation: no cursor cloning, speculation,
// checkpoints, or multi-token lookahead. Character composition belongs to the lexer.
} // namespace cel
#endif
