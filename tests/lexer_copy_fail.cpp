// SPDX-License-Identifier: BSL-1.0
#include "lexer.hpp"
#include <cel/parser.hpp>
/// Attempts to clone a primed lexer; compilation must fail.
int main() {
    cel::parser_detail::lex_lexer lexer("true");
    auto copy = lexer;
    return static_cast<int>(copy.current().span.begin);
}
