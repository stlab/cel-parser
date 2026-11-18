---
title: "Pin runtime source with a versioned CPM URI"
entity_type: decision
confidence: 0.99
created: 2026-11-18T06:31:11Z
last_accessed: 2026-11-18T06:31:11Z
last_reinforced: 2026-11-18T06:31:11Z
access_count: 1
sources:
  - "CMakeLists.txt:6"
  - "CMakeLists.txt:24"
  - "docs/evidence.md:94-95"
supersedes: "[[Pin CPM dependencies to reviewed commits]]"
tags: ["cmake", "cpm", "dependencies", "supply-chain"]
tier: knowledge
relationships:
  - type: relates_to
    target: "Pin CPM dependencies to reviewed commits"
  - type: relates_to
    target: "Keep cel-runtime out of parser installation"
  - type: relates_to
    target: "Initialize shared installation defaults before CPM runtime and parser targets"
---

# Pin runtime source with a versioned CPM URI

Pin `cel-runtime` with a CPM URI that includes both the compatibility release (`1.1.0`) and the reviewed full commit SHA. Keep the release tag-to-commit relationship in `docs/evidence.md`. `cpp-library` 6.0.2 resolves the dependency from CPM's registry, so do not add a separate `cpp_library_map_dependency` for this CPM package.
