---
title: "Admit numeric values by literal form, not converted binary64 payload"
entity_type: pattern
confidence: 0.99
created: 2026-11-11T05:46:06Z
last_accessed: 2026-11-11T05:46:06Z
last_reinforced: 2026-11-11T05:46:06Z
access_count: 1
sources:
  - "include/cel/parser.hpp:62-80"
  - "src/compiler.cpp:405-418"
  - "src/parser.cpp:373-429"
tags: ["parser", "literals", "rust-parity", "tuple-selectors"]
tier: knowledge
---

# Admit numeric values by literal form, not converted binary64 payload

The text lexer converts numeric tokens to binary64 even when numeric_form is integer. That payload does not make an integer spelling an f64 language value: canonical Rust retains integer literal kinds. Reject integer forms in primary-expression compilation, while retaining integer/radix tokens and raw spelling for tuple selectors. Rejecting integers globally in the lexer would break valid selectors; converting them to f64 in primary would violate the Rust acceptance subset. Keep actual floating spellings such as 1.0, 1e0 and 1.0f64 in positive expression examples and tests.
