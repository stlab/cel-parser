---
title: "Isolate Apple numeric conversion from the caller's floating-point environment"
entity_type: decision
confidence: 0.99
created: 2026-11-19T05:47:01Z
last_accessed: 2026-11-19T05:47:01Z
last_reinforced: 2026-11-19T05:47:01Z
access_count: 1
sources:
  - "src/parser.cpp:180-194"
  - "tests/tests.cpp:129-153"
  - "src/lexer.hpp:21-23"
  - "docs/lexical-contract.md:105-109"
tags: ["parser", "floating-point", "macos", "locale", "compatibility"]
tier: knowledge
---

# Isolate Apple numeric conversion from the caller's floating-point environment

`strtod_l(..., LC_C_LOCALE)` isolates the locale but still obeys the caller's rounding mode. Preserve the lexer's round-to-nearest contract by saving the environment with `std::feholdexcept`, selecting `FE_TONEAREST`, converting, and restoring the saved environment with `std::fesetenv` before checking errors.

Keep allocating or otherwise throwing C++ operations outside the save/restore region. Restore the caller's rounding mode and existing exception flags rather than leaking conversion state. The parser regression covers directed rounding, decimal values, radix ties, overflow, and underflow through real tokenization.
