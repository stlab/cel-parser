// SPDX-License-Identifier: BSL-1.0
#ifndef CEL_PARSER_ALL_HPP
#define CEL_PARSER_ALL_HPP
/// \file
/// Includes the expression compiler and checked library registration interfaces.
/// Prefer `<cel/parser.hpp>` for compilation or `<cel/parser/lookup.hpp>` for extensions.
/// Compile and execute an expression:
/// \code{.cpp}
/// #include <cel/parser/all.hpp>
///
/// int main() {
///     cel::parser compiler(cel::op_lookup{});
///     auto code = compiler.parse_str("1.0 + 2.0");
///     cel::raw_stack stack;
///     return code.prepare<double>().call(stack) == 3.0 ? 0 : 1;
/// }
/// \endcode
#include <cel/parser/compiler.hpp>
#endif
