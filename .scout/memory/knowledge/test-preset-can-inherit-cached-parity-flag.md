---
title: "Test preset can inherit cached parity flag"
entity_type: discovery
confidence: 0.97
created: 2026-11-18T06:35:07Z
last_accessed: 2026-11-18T06:35:07Z
last_reinforced: 2026-11-18T06:35:07Z
access_count: 1
sources:
  - "CMakeLists.txt:77-85"
  - "CMakePresets.json:15-28"
tags: ["cmake", "testing", "parity", "cache"]
tier: knowledge
votes_up: 1
votes_down: 0
last_voted: 2026-11-18T06:46:40Z
---

# Test preset can inherit cached parity flag

The `test` configure preset does not set `CEL_PARSER_ENABLE_PARITY`; CMake's `option(... OFF)` only supplies the default when the cache key is absent. Reusing a `build/test` cache previously configured with parity enabled can therefore require a stale or missing `CEL_PARSER_REFERENCE_SOURCE` and fail configuration. For the ordinary local suite, pass `-DCEL_PARSER_ENABLE_PARITY=OFF` or configure a fresh build directory; enable parity only with a valid local Rust reference checkout.
