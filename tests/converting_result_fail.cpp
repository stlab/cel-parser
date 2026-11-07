// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>

/// Fails compilation because exact registration never adapts a removed float result to f64.
int main() {
    cel::op_lookup lookup;
    lookup.register_op<double, double>("converted", [](double) -> float { return 1.0F; });
}
