# Lexical contract

This maintainer reference describes `cel::parser_detail` facilities declared in
`src/lexer.hpp`. Tokens, cursors, and lexing are not installed client interfaces.

Use this reference to implement a CEL token consumer or another flattened
producer. The text adapter consumes decoded CEL text, not JSON or Rust macro
token trees.

## Tokens and source ownership

`source_span` stores inclusive `begin` and exclusive `end` UTF-8 byte offsets.
`token` owns its variant payload and span. Identifiers, numeric spelling,
suffixes, doc text, and diagnostic messages remain valid when you copy a token
and destroy its source or lexer. Copies of mutable strings are independent.
Copying a token costs O(payload bytes); scalar, delimiter, end, and inline
punctuation payloads have constant storage.

`token_value` contains these alternatives:

| Alternative | Meaning |
| --- | --- |
| `bool` | Exact unprefixed `true` or `false` |
| `numeric_literal` | Binary64 scalar, exact spelling, suffix, and syntax form |
| `identifier` | Exact identifier spelling, including a raw `r#` prefix |
| `punctuation` | One to three inline ASCII punctuation bytes |
| `open_delimiter`, `close_delimiter` | A group boundary and its delimiter kind |
| `doc_comment` | Inner/outer distinction and content without comment markers |
| `end_token` | Terminal input |
| `parse_error` | Owned diagnostic with category, byte interval, and `what()` |

`punctuation::spelling()` borrows the punctuation object's inline storage;
keep that object alive and unchanged while you use the view. The constructor
throws `std::invalid_argument` for empty, oversized, or non-punctuation input.
The delimiter kinds are parenthesis, bracket, brace, and invisible `none`.
Only an external producer emits invisible groups.

## Traversal

`lex_lexer(std::string_view)` copies the source, validates UTF-8, and primes its
current token. A temporary argument is safe. `current()` returns a reference
that you must not retain across `advance()` or destruction. Copy the token if
you need a persistent value. `advance()` consumes one token, reaches end after
an error, and leaves end unchanged.

`token_cursor(std::span<const token>)` borrows an immutable flattened sequence.
The sequence must outlive the cursor. It primes the first token, preserves
producer payloads and spans, and synthesizes end at the last consumed token's
exclusive end, or zero for empty input. An explicit end is preserved. An error
advances to implicit end; tokens following an error or explicit end are ignored.
The producer supplies token validity and group structure; this cursor doesn't
re-lex or validate an external producer's groups.

Neither interface can be copied, assigned, moved, checkpointed, indexed, or
rewound. There is no parser lookahead beyond the current token. Character
composition inside the adapter doesn't expose additional parser lookahead.

For example, consume an external boolean and reach implicit end:

```cpp
std::array<cel::token, 1> tokens{{{{0, 4}, true}}};
cel::token_cursor cursor(tokens);
bool value = std::get<bool>(cursor.current().value);
cursor.advance();
```

## Identifiers and trivia

Identifiers use Unicode 18.0.0 `XID_Start` plus `_` for the first scalar and
`XID_Continue` thereafter. The adapter doesn't normalize spelling or treat
arbitrary non-ASCII scalars as identifiers. Raw identifiers retain `r#`;
`r#_`, `r#super`, `r#self`, `r#Self`, and `r#crate` are malformed.

Whitespace follows Rust `char::is_whitespace`, with U+200E and U+200F added.
It includes the Unicode `White_Space` scalars, not only ASCII whitespace.

Ordinary line comments begin with `//`, except `///` and `//!`; `////` remains
ordinary. Ordinary block comments begin with `/*`, except `/**` and `/*!`;
`/***` and `/**/` remain ordinary. Nested block comments use an iterative
depth counter. Line comments end at LF or end of input. Doc-line CRLF excludes
the CR from doc content. Doc block content preserves nested markers and CRLF;
a bare CR in doc content is malformed.

The adapter emits direct doc-comment metadata. It doesn't parse Rust `#[doc =
"..."]` attributes. `#` remains inline punctuation in decoded text, unlike the
Rust LexLexer attribute handler.

## Numbers and punctuation

Only bool and binary64 are built-in value types. The expression parser accepts
floating forms such as `1.0`, `1e0`, and `1.0f64`, and rejects integer value
forms, including `1`, radix integers, and `1f64`. Integer tokens remain available
to lexical consumers and tuple selectors; their converted binary64 payload
does not change their syntax classification.

The text lexer retains integer and decimal tokens. Its only accepted suffix is `f64`; `f32`,
integer suffixes, and arbitrary identifier suffixes are unsupported literals.
Decimal, binary `0b`, octal `0o`, and hexadecimal `0x` spellings retain their
source spelling and underscores. Radix prefixes are lowercase. The producer's
permissive underscore placements remain accepted, including `0o_17`, `1_`,
and `1_e_+_2`.

Conversion rounds to nearest binary64. Overflow yields positive infinity;
underflow rounds to a subnormal or zero. Signs are separate punctuation.
The converter is locale-independent and doesn't enable fast-math. Hexadecimal
digits take precedence over suffix interpretation: `0xff64` is an unsuffixed
hexadecimal integer, not an `f64`-suffixed number.

`numeric_form` records integer, dotted decimal, or exponent syntax separately
from the converted payload. The token for `1` has payload `1.0`, but the core
expression parser rejects it in a value position. Preserve
`numeric_literal::spelling` when implementing structural projection: `.0.1`
emits `.` followed by decimal token `0.1`. The expression parser must interpret
that raw decimal spelling as two chained tuple selectors, reject exponent and
suffixed selectors, and avoid deriving structural indices from rounded doubles.

`1..2` emits `1`, `..`, and `2`. A decimal dot doesn't consume a following dot
or identifier-start scalar: `1.foo` emits `1`, `.`, and `foo`. An incomplete
exponent can become an unsupported suffix under upstream tokenization: `1e+`
reports suffix `e` over bytes `[0,2)`, rather than converting a malformed
exponent or inventing a scalar.

The adapter combines adjacent punctuation into the longest supported compound:

```text
&& || == != <= >= << >> -> => := .. ..=
```

Whitespace or comments prevent combination. Other recognized punctuation stays
single-byte, including `:`, so `::` remains two tokens.

## Diagnostics, bounds, and adaptations

Lexical errors are terminal `parse_error` tokens, not successful defaults.
`parse_error` derives from `std::runtime_error`. Use `category()` and `span()`
for portable handling; messages aren't stable identifiers.

| Category | Text-adapter cases |
| --- | --- |
| `malformed_input` | Invalid UTF-8, invalid radix/boundary, unknown scalar, unfinished quote/comment, forbidden raw identifier, bare doc CR |
| `unsupported_literal` | Unsupported suffix or closed string, character, byte, raw, or C-string literal |
| `delimiter_error` | Unmatched close, mismatched close, or unfinished group |
| `resource_limit` | Reserved for another producer's explicitly documented limit; this adapter imposes no fixed depth cap |

The adapter validates all UTF-8 before emitting any token. Its invalid-UTF-8
diagnostic covers the first byte of the invalid sequence, including a truncated
sequence. Balanced groups emit individual boundary-byte spans. An unfinished
group diagnoses its innermost opening boundary. Full compound punctuation and
literal spans cover their decoded-text bytes.

These spans intentionally differ from Rust's first-punctuation compound spans,
whole-group delimiter spans, and line/column coordinates. Normalize spans to
bytes for differential comparisons; don't claim identical macro spans.

The adapter scans unsupported quoted literals to their closing marker, respecting
escapes and raw hashes, then diagnoses their full span. It doesn't validate every
Rust escape or character-literal rule for unsupported values. A bare apostrophe
followed by an identifier remains punctuation plus identifier, as in Rust lifetime
tokenization. This boundary is not a general Rust lexer.

Source copy and initial validation cost O(n) time and storage. Traversal costs
O(n + u log R), for n source bytes, u identifier scalars, and R generated
property ranges. Delimiter tracking costs O(d) storage for depth d, and current
owned payloads cost O(payload bytes). Block comments and groups don't recurse.
Standard allocation failures propagate; there is no hidden finite resource cap.
