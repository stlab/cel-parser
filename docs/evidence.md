# Source evidence and parser handoff

This reference records the source contracts and dependencies for tokens,
lookup/types, and retained expression compilation. It doesn't claim P4
Rust/C++ differential verification.

## Source revisions

The Rust source revision is `03d658f8b12acabe1d6a2503281e7c86cf00b6a3`.
Accepted sibling runtime packaging is at
`bf2d2ac9fe420139b85d1adec9d57c1565356e32`. The parser consumes its public installed
package; it neither copies runtime implementation nor includes private headers.

The delivered Rust evidence establishes these contracts:

| Source | Retained behavior |
| --- | --- |
| `cel-parser/src/lex_lexer.rs:123–158, 481–512` | Forward-only flattened groups and delimiter kinds |
| `cel-parser/src/lex_lexer.rs:180–224, 247–276` | Boolean specialization and the complete compound list |
| `cel-parser/src/lex_lexer.rs:398–448` | Literal, identifier, punctuation, delimiter, doc, and error alternatives |
| `cel-parser/src/lex_lexer.rs:1225–1239` | `5..10` range splitting |
| `cel-parser/src/error.rs:11–25, 108–144` | Portable source diagnostics and original-source byte conversion |
| `cel-parser/src/lib.rs:511–552` | Rust literal discrimination; approved C++ bool/f64-only adaptation |
| `cel-parser/src/lib.rs:1828–1870` | Structural tuple selectors, including decimal-token splitting for `.0.1` |

Rust's integer default differs deliberately: C++ unsuffixed numbers are binary64.
The text adapter's byte spans and direct doc metadata are interface adaptations;
see the [lexical contract](lexical-contract.md).

## Upstream rules and numeric conversion

The locked Rust dependencies are `proc-macro2` 1.0.107, Syn 2.0.119, and
`unicode-ident` 1.0.27. The delivered `cargo tree --locked --offline` evidence
resolves these versions.

Focused authoritative sources establish lexical details:

- [proc-macro2 identifier classifiers](https://github.com/dtolnay/proc-macro2/blob/1.0.107/src/fallback.rs#L841-L847)
  use `_` or XID_Start initially and XID_Continue thereafter.
- [proc-macro2 text adapter](https://github.com/dtolnay/proc-macro2/blob/1.0.107/src/parse.rs)
  defines `ident_any`, `float_digits`, `digits`, `skip_whitespace`,
  `doc_comment_contents`, and iterative `block_comment`.
- [Syn numeric normalization](https://github.com/dtolnay/syn/blob/2.0.119/src/lit.rs#L1747-L1940)
  preserves arbitrary-size radix integers and removes decimal underscores.
  `base10_parse` delegates normalized spelling to the selected Rust `FromStr`
  implementation.
- [unicode-ident tables](https://github.com/dtolnay/unicode-ident/blob/1.0.27/src/tables.rs)
  identify Unicode 18.0.0 as the upstream version.

A local `rustc` stdin probe of `str::parse::<f64>()` confirmed these outcomes:

| Spelling | Rust result |
| --- | --- |
| `1e9999` | Positive infinity |
| `1e-9999` | Zero |
| `1.7976931348623157e308` | Maximum finite binary64 |
| `1.7976931348623159e308` | Positive infinity |
| `5e-324` | Minimum positive subnormal |
| `2e-324` | Zero |
| `9007199254740993` | `9007199254740992.0` |
| `1e999999999999999999999` | Positive infinity |

The C++ implementation uses `std::from_chars`; it doesn't implement decimal
floating-point conversion. Because the standard converter leaves the output
unchanged on range failure, the adapter classifies a decimal's order as positive
or nonpositive and supplies infinity or zero only after a range error. Conversion
of representable values remains the standard converter's responsibility.
Power-of-two radix digits are regrouped exactly into hexadecimal digits before
the same converter runs; no intermediate fixed-width integer narrows the value.
All numeric traversal is linear and uses no locale-dependent conversion.

## Dependencies and licenses

Production code depends on the C++20 standard library, committed Unicode
property data, and the installed `cel-runtime` package through `cel::runtime`.
There is no Unicode shared-library dependency.

Build infrastructure uses `stlab/cpp-library` at
`b0b096964a93561c7dd1586fa7b0efdf5f077aab` under BSL-1.0. Its pinned scaffold
provides CMake presets, doctest 2.5.3 (MIT), generated negative-compilation tests,
CTest, exports, installation, CI, and strict Doxygen configuration.
Doxygen-awesome-css 2.5.0 (MIT) supplies the documentation theme. CPM.cmake
0.43.2 is MIT-licensed. Doxygen, Ninja, Clang tools, and Python are
build/generation tools, not exported package dependencies.

The parser source uses BSL-1.0; see `LICENSE`. Generated Unicode tables use
Unicode-3.0; see `licenses/Unicode-3.0.txt`.
Installation distributes both notices under `share/licenses/cel-parser` with
the default CMake data directory.

## Reproduce Unicode tables

The committed private tables contain 702 XID_Start ranges and 821 XID_Continue
ranges, merged into sorted disjoint intervals. Their input is Unicode 18.0.0
`DerivedCoreProperties.txt`, SHA-256
`09c928886a178fcafd93c29e4bd59073a058e5a100b716d425cb563ab50f68c9`.
The generator verifies that digest before writing output.

Regenerate and format the tables from the repository root:

```sh
python3 tools/generate_unicode.py
clang-format -i src/unicode_data.hpp
```

Confirm that `git diff -- src/unicode_data.hpp` is empty. Consumers don't need
Python or network access to use the committed tables.

## Expression compilation and remaining verification

Owned operation lookup, complete recursive type parsing, and retained bool/f64
emitters and retained expression productions are implemented. See the
[built-in census](builtin-census.md) and [expression grammar](expression-grammar.md)
for source-bound dispositions and contract tests. P4 differential parity and
final performance/platform review remain separate milestones.

The expression compiler consumes only `current()` and `advance()`. It retains
strict Wirth-style LL(1) separator laws, uses exact numeric spellings for
structural selectors, splits `.0.1`, and rejects scientific/suffixed tuple
selectors. Postfix `[expression]` is the approved C++ array-index adaptation
and uses the runtime's checked binary64-to-index boundary.

The lexer tests cover lifetime independence, full source-byte spans, comments,
Unicode names and malformed UTF-8, arbitrary-depth iterative traversal, numeric
boundaries, compound separation, producer cursor termination, and static
non-clonability. Future differential tests must normalize the documented
text-adapter differences rather than compare incidental exception messages.
