---
title: "Separate CEL constructor names from runtime record identities"
entity_type: decision
confidence: 1.00
created: 2026-11-18T01:41:05Z
last_accessed: 2026-11-18T01:41:05Z
last_reinforced: 2026-11-18T01:41:05Z
access_count: 1
sources:
  - "include/cel/parser/lookup.hpp:233-257"
  - "src/lookup.cpp:41-79"
  - "src/lookup.cpp:143-153"
tags: ["records", "parser", "identifiers", "registration"]
tier: knowledge
---

# Separate CEL constructor names from runtime record identities

Runtime record identities are nominal and may contain characters that CEL identifiers cannot spell, such as `sample.point`. Register an adapted record with an explicit CEL constructor alias via `register_record_type<T>(name)`; multiple aliases can refer to the same runtime descriptor without changing dispatch identity. Registration validates the alias and every nested record field against the actual lexer before inserting the type, and constructor aliases also reject contextual keywords `if`, `else`, and `as`. Keep this validation separate from the runtime's unrestricted record identities.
