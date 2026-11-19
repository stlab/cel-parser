---
title: "Delegate public-header checks and license installation to cpp-library"
entity_type: decision
confidence: 0.99
created: 2026-11-19T04:10:28Z
last_accessed: 2026-11-19T04:10:28Z
last_reinforced: 2026-11-19T04:10:28Z
access_count: 1
sources:
  - "CMakeLists.txt:17-31"
  - "CMakeLists.txt:43-50"
relationships:
  - type: relates_to
    target: "Keep cel-runtime out of parser installation"
tags: ["cmake", "cpp-library", "testing", "licenses"]
tier: knowledge
votes_up: 1
votes_down: 0
last_voted: 2026-11-19T22:08:49Z
---

# Delegate public-header checks and license installation to cpp-library

Keep the public-header list in `cpp_library_setup(HEADERS ...)` as the source for cpp-library's automatic standalone header checks in top-level testing builds. Do not recreate per-header generated translation units or object-library targets.

Let cpp-library install the root `LICENSE` automatically; declare the additional Unicode notice with `LICENSE_FILES licenses/Unicode-3.0.txt` rather than a separate `install(FILES ...)` block. Private `src` include directories remain necessary for white-box tests; standalone public-header checks do not replace that access.
