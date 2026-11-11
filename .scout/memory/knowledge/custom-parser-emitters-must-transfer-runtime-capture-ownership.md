---
title: "Custom parser emitters must transfer runtime capture ownership"
entity_type: pattern
confidence: 0.97
created: 2026-11-11T01:44:57Z
last_accessed: 2026-11-11T01:44:57Z
last_reinforced: 2026-11-11T01:44:57Z
access_count: 1
sources:
  - "src/lookup.cpp:88-95"
  - "src/lookup.cpp:114-120"
  - "src/lookup.cpp:208-215"
tags: ["ownership", "public-contracts", "operation-emitters"]
tier: knowledge
---

# Custom parser emitters must transfer runtime capture ownership

Custom operation emitters and scope callbacks execute during compilation; they are not automatically retained by the finished program. `register_operation` owns the emitter in lookup registry state, and operation application invokes that emitter and validates its resulting stack/type. A custom emitter must place every dependency needed during execution into the emitted operation's owned state, or document and uphold any borrow. Do not describe registry ownership as automatically extending all callback captures to program lifetime. The typed registration helper separately retains its shared callback in the emitted runtime thunk; preserve that distinction when editing public ownership contracts or adding registration helpers.
