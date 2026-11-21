---
title: "Keep cel-runtime out of parser installation"
entity_type: decision
confidence: 0.99
created: 2026-11-18T06:31:07Z
last_accessed: 2026-11-18T06:31:07Z
last_reinforced: 2026-11-18T06:31:07Z
access_count: 1
sources:
  - "CMakeLists.txt:17-45"
  - "docs/validation.md:117-138"
supersedes: "[[Initialize shared installation defaults before CPM runtime and parser targets]]"
tags: ["cmake", "cpm", "dependencies", "installation"]
tier: knowledge
relationships:
  - type: relates_to
    target: "Pin CPM dependencies to reviewed commits"
  - type: relates_to
    target: "Initialize shared installation defaults before CPM runtime and parser targets"
votes_up: 2
votes_down: 0
last_voted: 2026-11-19T04:10:10Z
---

# Keep cel-runtime out of parser installation

Let `cpp_library_setup` initialize the default `CEL_INSTALL` option before adding `cel-runtime`. Add the runtime with its versioned commit URI and keep CPM's short-form `EXCLUDE_FROM_ALL` default; do not override it to `NO`. CPM registry resolution supplies the package/version without an explicit `cpp_library_map_dependency`. The cpp-library 6.2 presets disable installation except in the explicit install preset. Validate source builds; do not add installation, package-artifact, or installed-consumer gates.
