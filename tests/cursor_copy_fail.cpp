// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
/// Attempts to clone a forward-only cursor; compilation must fail.
int main() {
    cel::token_cursor cursor(std::span<const cel::token>{});
    auto copy = cursor;
    return static_cast<int>(copy.current().span.begin);
}
