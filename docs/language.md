# Supported CEL language {#parser_language}

This page describes the subset accepted by the C++ parser. It is not the full
Rust toolkit or a standard-library installation. Compile a complete decoded
UTF-8 expression with `cel::parser::parse_str`; trailing input is an error.

## Scalar values and operators

The built-in scalar types are `bool` and IEEE binary64 `f64`, represented by
C++ `bool` and `double`. Numeric values require floating syntax such as `1.0`,
`1e0`, or `1.0f64`. Integer literals are not converted to f64.

The core provides these exact operations:

| Operands | Operations |
| --- | --- |
| f64 | Binary `+`, `-`, `*`, `/`, `%`; unary `-`; equality and ordering comparisons |
| bool | Unary `!`; `==` and `!=`; short-circuit `&&` and `\|\|` |
| Identity casts | `f64 as f64` and `bool as bool` only |

`%` uses floating remainder (`std::fmod`). Division follows binary64
infinity/NaN behavior. Bool ordering, unary `+`, numeric conversions, and
implicit coercions are not supplied.

Precedence, from tightest to loosest, is postfix calls/member selection,
unary operators, `as`, multiplication/division/remainder, addition/subtraction,
shifts, bitwise AND/XOR/OR, comparisons, `&&`, `||`, and ranges.
Comparisons do not chain. Bitwise and shift syntax has no built-in bool/f64
signature; it needs a matching client registration.

## Collections, records, and ranges

The parser accepts these structural forms:

| Form | Example and restriction |
| --- | --- |
| Group or unit | `(1.0)` is grouping; `()` is the empty tuple |
| Tuple | `(true, 2.0)`; `(2.0,)` requires the singleton comma |
| Tuple selection | `(true, 2.0).1`; integer tokens are selectors, not integer values |
| Homogeneous array | `[1.0, 2.0]`; every element has a compatible complete type |
| Typed array | `[]:[f64]`; the annotation names the whole array type and is required for an empty array |
| Nominal record | `Point{x: 1.0, y: 2.0}` and `.x`, after registering that schema |
| Range | `1.0..2.0`, `1.0..=2.0`, `1.0..`, `..2.0`, `..=2.0`, or `..` |

Lists do not accept trailing commas, except for the required singleton-tuple
comma. Postfix bracket indexing such as `array[0.0]` is not part of the
grammar. There is no universal aggregate equality. Ranges construct values;
they do not provide floating-point iteration.

Type annotations support `bool`, `f64`, registered names, tuples such as
`(bool, f64)`, arrays such as `[f64]`, and complete callable signatures such as
`Fn((f64,), f64)`. A registered name does not add a new `as` cast target.

## Conditions, closures, and calls

An `if` expression selects one of two compatible result types:

```text
if true { 2.0 } else { 3.0 }
(|x:f64| x * 2.0)(3.0)
(|| 7.0)()
```

Both branches type-check, but only the selected branch executes. A missing
`else` supplies unit, so the then-branch must also produce unit. Bodies contain
one expression, not statements or general blocks. Parenthesize record literals
used inside condition headers to distinguish them from the body delimiter.

Closures declare every parameter with an explicit type. Use `|| expression`
for zero parameters, not separated `| |`. Parameters admit bool/f64 and their
supported composites; results can use any valid complete type. Closures do not
capture enclosing parameters or transient scopes. Persistent library
registrations remain visible.

Ordinary calls (`identity(4.0)`), receiver calls (`3.0.twice()`), named member
selection, and custom operators need matching registrations. Syntax alone
does not supply a library operation. See
[Add an extension library](libraries.md).

## Unsupported features and limits

The core does not supply integer or f32 values, string values, a standard
library, implicit numeric casts, postfix bracket indexing, general statements,
or ambient closure capture. Registering a client type does not add literal
syntax or built-in operations for it.

Compilation limits active expression/conditional nesting to 128 productions
and type-annotation nesting separately to 128 productions. Flat chains are
iterative rather than consuming this allowance. Diagnostics report UTF-8 byte
intervals; source strings need not outlive successfully compiled programs.

## Read the CEL language book

See the deployed [CEL language book](https://stlab.github.io/cel-rs/cel-book/)
for the broader language, examples, and standard-library reference. Use the
subset and registration rules on this page to determine which book examples
apply to this C++ port.
