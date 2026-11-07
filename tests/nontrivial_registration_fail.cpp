// SPDX-License-Identifier: BSL-1.0
#include <cel/parser.hpp>
/// Rejects a nontrivial opaque client leaf instead of inferring native ownership.
int main() {
    cel::op_lookup lookup;
    lookup.register_type<std::string>("Text");
}
