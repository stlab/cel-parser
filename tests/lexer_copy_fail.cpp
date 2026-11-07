// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
/// Attempts to clone a primed lexer; compilation must fail.
int main() {
    cel::lex_lexer lexer("true");
    auto copy = lexer;
    return static_cast<int>(copy.current().span.begin);
}
