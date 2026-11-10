# Retained built-in census {#builtin_census}

This reference lets parser maintainers check the retained operation inventory
against Rust revision `03d658f8b12acabe1d6a2503281e7c86cf00b6a3`.
P4 adds differential and equivalent public-interface evidence, mapped in the
[feature closure](p4-handoff.md). Use the corrected runtime prerequisite pinned
in [source evidence](evidence.md).

## Source and dispatch anchors

The census uses these executable producer and consumer anchors:

| Anchor | Responsibility |
| --- | --- |
| [Core table and selector](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/op_table.rs#L1024-L1105) | Exact core names, static signatures, and shift dispatch |
| [Scoped resolution](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/op_table.rs#L2030-L2137) | Ordinary/member selection, custom precedence, and callable fallback |
| [Registered emitter selection](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/registered_ops.rs#L414-L489) | Complete operand matching and first-marker ordinary calls |
| [Standard installer](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-std/src/math.rs#L61-L129) | Eleven persistent standard numeric names and exact Rust methods |
| [Public standard-library installation](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-std/src/lib.rs#L13-L35) | Opt-in installation; persistent registrations survive isolation |
| [Type expression resolution](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/type_expr.rs#L14-L126) | Recursive tuples, arrays, named types, and complete Fn results |
| [Cast target routing](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/op_table.rs#L1370-L1395) | Closed target spellings; registered aliases aren't cast targets |
| [Cast and unary consumption](https://github.com/stlab/cel-rs/blob/03d658f8b12acabe1d6a2503281e7c86cf00b6a3/cel-parser/src/lib.rs#L1688-L1727) | Casts consume an identifier, not a full type; unary prefixes are `-` and `!` only |

`op_lookup` installs core operations, identity casts, and the persistent
`range_full` scope by default. `install_standard_library` installs standard
numeric functions explicitly; constructing a lookup doesn't install them.
Client operation/structural registrations precede core built-ins. Standard
functions are persistent registrations and compose with disjoint client
overloads. Typed operation callbacks execute only when compiled runtime code runs.
Scope callbacks and descriptor emitters execute during compilation.

## Eager retained signatures

Each row names exact arguments/results. `D` means `f64`; `B` means `bool`.
Every row is implemented with public checked runtime emission. The test anchors
name cases in `tests/lookup_test.cpp`.
Source syntax/precedence and standard-name call selectors are also covered by
`retained precedence` and `standard numeric names` in `tests/expression_test.cpp`.
See the [expression grammar](expression-grammar.md) for every emission edge.

| Name | Exact signature | Rust producer in `op_table.rs` unless noted | Semantics and test evidence |
| --- | --- | --- | --- |
| `+` | `(D, D) -> D` | L252 | IEEE addition; retained operators |
| `-` | `(D, D) -> D` | L301 | Source-order IEEE subtraction; retained operators |
| `-` | `(D) -> D` | L328 | Unary negation, including zero sign; retained operators |
| `*` | `(D, D) -> D` | L374 | IEEE multiplication; retained operators |
| `/` | `(D, D) -> D` | L439 | Zero divisor yields infinity/NaN, not an integer error; retained operators |
| `%` | `(D, D) -> D` | L504 | Rust remainder, implemented with `fmod`, not IEEE `remainder`; retained operators |
| `!` | `(B) -> B` | L782-L783 | Logical negation; retained operators |
| `==` | `(B, B) -> B`, `(D, D) -> B` | L804-L806 | Numeric NaN is unequal to itself; signed zeros compare equal; retained operators |
| `!=` | `(B, B) -> B`, `(D, D) -> B` | L830-L832 | Negates the corresponding equality; retained operators |
| `<` | `(D, D) -> B` | L854 | IEEE partial comparison; retained operators |
| `<=` | `(D, D) -> B` | L878 | IEEE partial comparison; retained operators |
| `>` | `(D, D) -> B` | L900-L924 | IEEE partial comparison; retained operators |
| `>=` | `(D, D) -> B` | L924 | IEEE partial comparison; retained operators |
| `as f64` | `(D) -> D` | L1341-L1344 | Identity retains signed zero; range emitters and casts |
| `as bool` | `(B) -> B` | L1350-L1357 | Identity only; range emitters and casts |
| `range` | `(D, D) -> Range(D)` | L948 | Constructs `start..end`, no iteration; range emitters and casts |
| `range_inclusive` | `(D, D) -> RangeInclusive(D)` | L967 | Constructs `start..=end`; range emitters and casts |
| `range_from` | `(D) -> RangeFrom(D)` | L984 | Constructs `start..`; range emitters and casts |
| `range_to` | `(D) -> RangeTo(D)` | L1001 | Constructs `..end`; range emitters and casts |
| `range_to_inclusive` | `(D) -> RangeToInclusive(D)` | L1018 | Constructs `..=end`; range emitters and casts |
| `range_full` | `() -> RangeFull()` | L1631-L1640 | Persistent scope constructs bare `..`; canonical C++ metadata retains D endpoint identity; range emitters and casts |
| `min` | `(D, D) -> D` | `cel-std/src/math.rs:L76-L80` | Rust `min`, ignores one NaN; equal zeros may choose either operand; standard numeric names |
| `max` | `(D, D) -> D` | `cel-std/src/math.rs:L81-L85` | Rust `max`, ignores one NaN; equal zeros may choose either operand; standard numeric names |
| `clamp` | `(D, D, D) -> D` | `cel-std/src/math.rs:L86-L92` | Rejects bounds unless `lo <= hi`, including NaN bounds; NaN value remains NaN; standard numeric names |
| `abs` | `(D) -> D` | `cel-std/src/math.rs:L121` | Floating absolute value, clears zero sign; standard numeric names and rounding/IEEE helpers |
| `signum` | `(D) -> D` | `cel-std/src/math.rs:L122` | NaN remains NaN; +0 gives +1, -0 gives -1; standard numeric names |
| `sqrt` | `(D) -> D` | `cel-std/src/math.rs:L123` | Negative input gives NaN; preserves -0; standard numeric names and rounding/IEEE helpers |
| `floor` | `(D) -> D` | `cel-std/src/math.rs:L124` | Toward negative infinity; standard numeric names |
| `ceil` | `(D) -> D` | `cel-std/src/math.rs:L125` | Toward positive infinity; standard numeric names |
| `trunc` | `(D) -> D` | `cel-std/src/math.rs:L126` | Toward zero; standard numeric names |
| `fract` | `(D) -> D` | `cel-std/src/math.rs:L127` | `x - trunc(x)`, including infinity-to-NaN; standard numeric names and rounding/IEEE helpers |
| `round_ties_even` | `(D) -> D` | `cel-std/src/math.rs:L128` | Halfway-to-even, independent of ambient rounding mode; standard numeric names and rounding/IEEE helpers |

These rows represent 34 retained eager signatures, including casts and the
zero-operand full-range constructor. Standard numeric bare names push the
first owned marker; ordinary `"()"` calls and direct member operations select
the same persistent signatures. Tests exercise both selectors and cached
capture lifetime. `round` is deliberately absent; Rust tests that rejection at
`cel-std/src/math.rs:721–731`.
Cast targets retain only the spellings `bool` and `f64`. A registered alias of
either leaf is valid in type syntax but isn't a cast target. Unary `+` is absent,
not an identity operation. Tests reject both extensions.

## Structural and lazy expression completion

These families use runtime-described complete shapes, not one concrete nested
template specialization. `T` and `R` denote arbitrary valid complete types;
callable parameters retain their stricter eligibility rules.

| Family | Exact effect / dispatch | Rust source anchor | Implemented P3 syntax/emission and focused test |
| --- | --- | --- | --- |
| Tuple/unit construction | Source-ordered `(T0, ..., Tn) -> tuple(T0, ..., Tn)` | `cel-parser/src/lib.rs:2075–2160` | `()` / `(x,)` / `(x,y)` use `make_tuple`; `tuple group unit` |
| Tuple projection | `tuple(T0, ..., Tn) -> Ti` | `cel-parser/src/lib.rs:1828–1870` | `.N`, including `.0.1` raw splitting, uses `project_tuple`; `tuple selectors` |
| Array construction | Homogeneous `(T ... T) -> [T]`, including annotated empty arrays | `cel-parser/src/lib.rs:2182–2247` | `[x,y]:[T]` / `[]:[T]` use `make_array`, bound empty type witnesses; `tuple group unit`, `closures admit complete` |
| Array index | `([T], D) -> T` for checked finite integral in-bounds D | Rust has no bracket postfix at `cel-parser/src/lib.rs:1782–1784` | Approved C++ postfix `[expression]` uses `index_array`; `explicit C++ array indexing` |
| Nominal records | Schema-complete field construction and receiver-first typed selection | `cel-parser/src/lib.rs:2006–2071`, supplied record evidence | `Name{field:expr}` maps source order through `make_record`; `.field` uses member lookup; `registered names`, `semantic record parameters` |
| Named function values | Bare name -> first marker; `(marker, Args...) -> R` through direct signatures | `cel-parser/src/registered_ops.rs:303–334,448–489` | Bare names / `name(args)` use ordinary zero/marker-inclusive lookup; `registered names`, `standard numeric names` |
| Installed callable invocation | `(Fn((Args...), R), Args...) -> R` | `cel-parser/src/op_table.rs:2125–2137` | `(callee)(args)` uses ordinary `"()"`, public `invoke_closure`; `closures admit complete` |
| Closure creation | Independent body/parameters -> installed Fn value with any complete R | `cel-parser/src/lib.rs:2416–2491` | `|| expr` / `|name:T| expr` uses isolated formals and `push_closure`; `closures admit complete`, `independent closure scope` |
| Lazy `&&`, `\|\|` | Bool LHS plus selected dependent bool RHS -> bool | `cel-parser/src/lib.rs:1322–1372`, `cel-parser/src/parser_context.rs:874–894` | `a && b` / `a \|\| b` use dependent `logical_and` / `logical_or`; `lazy branches` |
| Conditional | Bool condition plus one selected dependent body -> shared complete result | `cel-parser/src/lib.rs:2507–2582`, `cel-parser/src/parser_context.rs:897–918` | `if cond {a} [else {b}/else if ...]` uses dependent `if_else`; absent else emits unit; `lazy branches` |

No universal tuple/array equality, floating range iteration, or `len` builtin
appears in the core table or standard installer. Tuple-shaped operations are
explicit runtime registrations, not default signatures
(`cel-parser/src/op_table.rs:1698–1768`). Clients can register complete structural
emitters or scope callbacks; the parser doesn't reinterpret their results.

## Removed-scalar and unsupported signatures

`I` denotes each of `u8`, `u16`, `u32`, `u64`, `u128`, `usize`, `i8`, `i16`,
`i32`, `i64`, `i128`, and `isize`; `F` denotes each of `f32` and `f64`.
These type sets are source-defined at `cel-parser/src/op_table.rs:1475–1491`.
The following signatures are not retained:

| Source inventory | Removed or absent signature | Disposition |
| --- | --- | --- |
| Arithmetic tables L210-L505 | `(I, I) -> I`, f32 counterparts, signed unary negation, and `(String, String) -> String` addition | Reject built-in use; no integer-to-D coercion or string replacement |
| Bitwise/shift tables L507-L778 and selector L1101-L1105 | Integer AND/OR/XOR and integer shift signatures | No retained bool/f64 signature; reject instead of inventing floating bit operations |
| Comparison tables L786-L928 | Integer/f32 comparisons and String comparisons | Removed scalar signatures; bool ordering is absent, not adapted |
| Cast producers L1183-L1302 | All `(I) -> I`, `(F) -> I`, `(B) -> I` signatures | Removed result type; no D return adaptation or floating truncation surrogate |
| Floating cast tables L1304-L1347 | f64->f32, f32->f64/f32, integer->f32/f64 | Removed input or result; only D identity remains |
| String cast table L1363-L1367 | `(String) -> String` | Removed scalar; B/D-to-String and numeric-to-B aren't registered Rust casts |
| Range tables L932-L1019 and generics L1525-L1570 | Integer/f32 endpoint range forms | Removed endpoints; Range(bool) is not registered; no iteration adaptation |
| Standard installer `math.rs:L74-L128` | Integer/f32 min/max/clamp; signed integer abs/signum; f32 unary functions | Removed scalar signatures; only eleven D signatures remain |
| Standard installer `math.rs:L61-L129` | `round`, `pow`, `powf`, trigonometric helpers, structural length helpers | Not installed by this Rust standard library; no invented library names/results |
| Core/static installer and client boundaries | Built-in arithmetic on registered leaves, records, arrays, or tuples without a signature | Explicit no-match unless a client registers that operation |

Tests reject removed type names, converting callbacks, wrong nominal operands,
unsupported casts and bitwise names, missing functions, and composite equality.
Expression tests preserve these dispatch failures. P4 `type-lookup-errors`
fixtures add shared rejection comparisons; removed scalar spellings and
C++ parameter exclusions retain their explicit own-contract tests rather
than being recast as supported signatures.
