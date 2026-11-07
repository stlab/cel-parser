// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>

/// Checks scalar lexing through the installed public header.
int main() try {
    cel::lex_lexer lexer("1f64");
    if (std::get<cel::numeric_literal>(lexer.current().value).value != 1.0) return 1;
    lexer.advance();
    return std::holds_alternative<cel::end_token>(lexer.current().value) ? 0 : 1;
} catch (const std::exception&) {
    return 1;
}
