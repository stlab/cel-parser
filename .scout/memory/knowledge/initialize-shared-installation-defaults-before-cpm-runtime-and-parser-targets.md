---
title: "Initialize shared installation defaults before CPM runtime and parser targets"
entity_type: decision
confidence: 0.10
created: 2026-11-10T22:04:27Z
last_accessed: 2026-11-18T05:05:33Z
last_reinforced: 2026-11-18T04:09:44Z
access_count: 4
sources:
  - "CMakeLists.txt:20-29"
  - "CMakeLists.txt:30-49"
  - "tests/installed_consumer.cmake.in:3-6"
  - "CMakeLists.txt:22-25"
  - "CMakeLists.txt:28-47"
  - "tests/installed_consumer.cmake.in:3-7"
  - "tests/installed_consumer.cmake.in:30-45"
  - "tests/installed_consumer.cmake.in:54-78"
tags: ["cmake", "cpm", "dependencies", "installation"]
tier: knowledge
votes_up: 1
votes_down: 1
last_voted: 2026-11-18T06:30:35Z
superseded_by: "[[Keep cel-runtime out of parser installation]]"
---

# Initialize shared installation defaults before CPM runtime and parser targets

Initialize CEL_INSTALL before acquiring cel-runtime: both libraries share this option. Acquire the runtime before setting up the parser so the runtime documentation hook does not capture the parser docs target. CPM URI shorthand excludes the dependency from the default build and recursive installation unless EXCLUDE_FROM_ALL is explicitly set to NO. Preserve that override so installed consumers receive cel-runtime. Export the minimum compatible runtime version; the installed-consumer test checks both packages resolve from the installation and rejects an older runtime.
