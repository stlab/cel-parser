---
title: "Pin runtime source with a versioned CPM URI"
entity_type: decision
confidence: 0.99
created: 2026-11-18T06:31:11Z
last_accessed: 2026-11-18T06:31:11Z
last_reinforced: 2026-11-18T06:31:11Z
access_count: 1
sources:
  - "CMakeLists.txt:5-6"
  - "CMakeLists.txt:33-35"
  - "docs/evidence.md:96-100"
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
votes_up: 2
votes_down: 0
last_voted: 2026-11-19T04:10:12Z
---

# Pin runtime source with a versioned CPM URI

Pin `cel-runtime` with a CPM URI that includes both the compatibility release (`1.1.2`) and reviewed full commit SHA `a819042cfa3fab8ede82052518f2b7f5175af5ad`. Keep the release tag-to-commit relationship in `docs/evidence.md`. `cpp-library` resolves the dependency from CPM's registry, so do not add a separate `cpp_library_map_dependency` for this CPM package.
