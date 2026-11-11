// SPDX-License-Identifier: BSL-1.0
#include <cel/parser/lookup.hpp>

/// Rejects the removed parser-owned standard-library installer.
int main() {
    cel::op_lookup lookup;
    cel::install_standard_library(lookup);
}
