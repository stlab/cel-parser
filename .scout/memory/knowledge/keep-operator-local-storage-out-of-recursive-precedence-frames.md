---
title: "Keep operator-local storage out of recursive precedence frames"
entity_type: discovery
confidence: 0.98
created: 2026-11-11T00:46:42Z
last_accessed: 2026-11-11T00:46:42Z
last_reinforced: 2026-11-11T00:46:42Z
access_count: 1
sources:
  - "src/compiler.cpp:258-291"
  - "tests/expression_test.cpp:467-510"
tags: ["parser", "recursion", "stack-usage", "MSVC"]
tier: knowledge
---

# Keep operator-local storage out of recursive precedence frames

Keep recursive precedence control in expression_compiler::binary separate from operator-local storage in binary_rhs (src/compiler.cpp:258-291). Every nested expression traverses the fixed precedence levels. Placing dyn_segment RHS builders and operator metadata in binary reserves their storage in every unoptimized frame, including frames that never encounter an operator; this can overflow the native MSVC Debug stack before the 128-production resource guard fires. binary_rhs runs only after operator_at(level), preserving operator consumption, lazy RHS ownership, source spans, lookup order, and non-chaining comparisons. Preserve the existing nesting contract rather than lowering its limit or enlarging test stacks. The public contract regression covers 32 and 127 parenthesis groups, resource rejection of excess nesting, and long iterative flat chains (tests/expression_test.cpp:467-510). Run that case with iterator debugging enabled when changing these frames.
