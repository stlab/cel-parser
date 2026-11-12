// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_COMPILER_HPP
#define CEL_PARSER_COMPILER_HPP
#include <cel/parser/lookup.hpp>

/// \file
/// Provides CEL expression compilation for clients with an owned operation registry.
/// See `<cel/parser/lookup.hpp>` for library registrations.

namespace cel {
/// \addtogroup parser_compilation
/// @{

namespace parser_detail {
struct parser_access;
} // namespace parser_detail
/// Owns lookup state and compiles CEL expressions into reusable checked programs.
/// Each successful call returns a program owning its values, descriptors, and emitted runtime
/// callbacks; the source and parser need not survive the program.
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
///     return compiler.parse_str("1.0 + 2.0");
/// }();
/// cel::raw_stack stack;
/// double result = code.prepare<double>().call(stack); // 3.0
/// \endcode
class parser {
    friend struct parser_detail::parser_access;
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
    /// Numeric values require floating forms such as 1.0 or 1e0; integer forms are unsupported.
    /// Postfix array indexing is not part of the core grammar.
    /// Returns an owned program under this class's ownership and failure contracts.
    /// Throws source-located parse_error for malformed_input, unsupported_literal, type_error,
    /// lookup_error, delimiter_error, or resource_limit as classified by error_category.
    /// Trailing input is malformed_input. More than 128 simultaneously active expression and
    /// conditional productions is resource_limit; iterative flat chains do not consume this
    /// nesting allowance. Type annotations have a separate limit of 128 active productions.
    /// Recursive entry throws std::logic_error as documented by this class.
    /// Client-emitter exceptions and allocation failures propagate without relabeling.
    /// Complexity: O(lexer traversal + emitted operations + lookup/type/structural metadata work)
    /// time, O(source bytes + code/captures + compilation metadata + bounded nesting) storage.
    /// Example: `auto code = p.parse_str("(|| 7.0)()");`.
    compiled_segment parse_str(std::string_view source);
};
// [knowledge] Preserve current-token LL(1) compilation: no cursor cloning, speculation,
// checkpoints, or multi-token lookahead. Character composition belongs to the lexer.

/// @}
} // namespace cel
#endif
