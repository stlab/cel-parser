---
title: "Initialize shared installation defaults before CPM runtime and parser targets"
entity_type: decision
confidence: 0.99
created: 2026-11-10T22:04:27Z
last_accessed: 2026-11-10T22:04:27Z
last_reinforced: 2026-11-10T22:04:27Z
access_count: 1
sources:
  - "CMakeLists.txt:20-29"
  - "CMakeLists.txt:30-49"
  - "tests/installed_consumer.cmake.in:3-6"
tags: ["cmake", "cpm", "dependencies", "installation"]
tier: knowledge
---

# Initialize shared installation defaults before CPM runtime and parser targets

cel-parser acquires cel-runtime as a pinned CPM source dependency before cpp_library_setup. Initialize CEL_INSTALL from PROJECT_IS_TOP_LEVEL before adding the dependency: both libraries use the cel namespace and therefore share cpp-library's installation option. Keep dependency acquisition before parser target setup; the runtime's directory-local docs post-build hook must not see the parser's docs target. Scope BUILD_TESTING OFF through CPM OPTIONS so runtime development targets do not collide with parser tests, without disabling the parser's own tests. The installed-consumer test installs both libraries recursively from this build tree; it must not infer an installed runtime prefix from source-target include directories.
