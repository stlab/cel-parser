---
title: "Keep cel-runtime out of parser installation"
entity_type: decision
confidence: 0.99
created: 2026-11-18T06:31:07Z
last_accessed: 2026-11-18T06:31:07Z
last_reinforced: 2026-11-18T06:31:07Z
access_count: 1
sources:
  - "CMakeLists.txt:22-24"
  - "CMakeLists.txt:42"
  - "docs/validation.md:121-130"
supersedes: "[[Initialize shared installation defaults before CPM runtime and parser targets]]"
tags: ["cmake", "cpm", "dependencies", "installation"]
tier: knowledge
relationships:
  - type: relates_to
    target: "Pin CPM dependencies to reviewed commits"
  - type: relates_to
    target: "Initialize shared installation defaults before CPM runtime and parser targets"
---

# Keep cel-runtime out of parser installation

Set `CEL_INSTALL` before adding `cel-runtime` so both projects see the intended option. Add the runtime with its versioned commit URI and keep CPM's short-form `EXCLUDE_FROM_ALL` default; do not override it to `NO`. With `cpp-library` 6.0.2, CPM registry resolution supplies the package/version without an explicit `cpp_library_map_dependency`. The source-consumer test remains; installed-consumer testing is intentionally omitted, and the generated package check verifies component presence only.
