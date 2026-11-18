---
title: "Pin CPM dependencies to reviewed commits"
entity_type: decision
confidence: 0.10
created: 2026-11-18T05:29:20Z
last_accessed: 2026-11-18T05:29:20Z
last_reinforced: 2026-11-18T05:29:20Z
access_count: 1
sources:
  - "CMakeLists.txt:28"
  - "CMakeLists.txt:48"
  - "docs/evidence.md:16-18"
tags: ["cmake", "cpm", "dependencies", "supply-chain", "installation"]
tier: knowledge
relationships:
  - type: relates_to
    target: "Initialize shared installation defaults before CPM runtime and parser targets"
votes_up: 0
votes_down: 2
last_voted: 2026-11-18T06:46:45Z
superseded_by: "[[Pin runtime source with a versioned CPM URI]]"
---

# Pin CPM dependencies to reviewed commits

Select CPM source dependencies with an immutable upstream commit SHA rather than a movable release tag. Keep the public package compatibility requirement separate in `cpp_library_map_dependency`, and record the release tag-to-commit relationship in evidence documentation. Package-version validation and installed-consumer checks establish compatibility; they do not pin which source revision CPM fetches.
