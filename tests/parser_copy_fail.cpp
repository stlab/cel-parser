// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
/// Attempts to duplicate mutable compiler/lookup ownership.
int main() {
    cel::parser first(cel::op_lookup{});
    auto second = first;
    (void)second;
}
