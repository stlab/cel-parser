---
title: "Pin runtime source with a versioned CPM URI"
entity_type: decision
confidence: 0.97
created: 2026-11-18T06:31:11Z
last_accessed: 2026-11-22T00:33:52Z
last_reinforced: 2026-11-22T00:33:52Z
access_count: 4
sources:
  - "CMakeLists.txt:5-6"
  - "CMakeLists.txt:33-35"
  - "docs/evidence.md:12-20"
  - "docs/evidence.md:96-101"
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
votes_down: 1
last_voted: 2026-11-22T00:33:52Z
---

# Pin runtime source with a versioned CPM URI

Pin `cel-runtime` with a CPM URI that includes both its compatibility release and the reviewed full commit SHA. `CMakeLists.txt` is the canonical pin; keep its release-to-commit relationship synchronized with `docs/evidence.md` when upgrading. Do not copy a fixed release or SHA from this memory into the build. `cpp-library` resolves the dependency from CPM's registry, so do not add a separate `cpp_library_map_dependency` for this CPM package.
