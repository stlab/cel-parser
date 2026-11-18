---
title: "Admit numeric values by literal form, not converted binary64 payload"
entity_type: pattern
confidence: 0.99
created: 2026-11-11T05:46:06Z
last_accessed: 2026-11-18T06:48:35Z
last_reinforced: 2026-11-18T06:48:35Z
access_count: 2
sources:
  - "include/cel/parser.hpp:62-80"
  - "src/compiler.cpp:405-418"
  - "src/parser.cpp:373-429"
  - "src/compiler.cpp:415-419"
  - "src/parser.cpp:379-422"
  - "src/lexer.hpp:15-25"
  - "tests/expression_test.cpp:157-194"
tags: ["parser", "literals", "rust-parity", "tuple-selectors"]
tier: knowledge
votes_up: 1
votes_down: 0
last_voted: 2026-11-18T06:48:54Z
---

# Admit numeric values by literal form, not converted binary64 payload

The parser admits decimal integer-form tokens as binary64 values only when they have the `f64` suffix, such as `1f64`; bare integers and radix-prefixed forms remain unsupported as values. Keep integer/radix token spelling available to tuple selectors, and apply the value-form check in primary-expression compilation rather than globally rejecting tokens. The lexer also requires an exponent sign immediately after `e` or `E`; underscores may follow the sign, but not precede it.
