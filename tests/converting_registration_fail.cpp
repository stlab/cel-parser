// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
/// Rejects an implicit operand conversion at the public registration boundary.
int main() {
    cel::op_lookup lookup;
    lookup.register_op<double, double>("wrong", [](float x) -> double { return x; });
}
