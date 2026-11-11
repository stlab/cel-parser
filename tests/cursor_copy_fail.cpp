// SPDX-License-Identifier: BSL-1.0
#include "lexer.hpp"
#include <cel/parser.hpp>
/// Attempts to clone a forward-only cursor; compilation must fail.
int main() {
    cel::parser_detail::token_cursor cursor(std::span<const cel::parser_detail::token>{});
    auto copy = cursor;
    return static_cast<int>(copy.current().span.begin);
}
