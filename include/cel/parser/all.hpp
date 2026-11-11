// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_ALL_HPP
#define CEL_PARSER_ALL_HPP
/// \file
/// Includes the token, lookup, and expression compiler interfaces for CEL clients.
/// Use `<cel/parser.hpp>` for token-only consumers or `<cel/parser/lookup.hpp>` for type parsing.
/// Compile and execute an expression:
/// \code{.cpp}
/// #include <cel/parser/all.hpp>
///
/// int main() {
///     cel::parser compiler(cel::op_lookup{});
///     auto code = compiler.parse_str("1 + 2");
///     cel::raw_stack stack;
///     return code.prepare<double>().call(stack) == 3.0 ? 0 : 1;
/// }
/// \endcode
#include <cel/parser/compiler.hpp>
#endif
