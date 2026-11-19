# Validate retained contracts {#parser_validation}

This guide serves library maintainers. It covers public cross-language
exact-source subset checks, diagnostic differences, and package artifact checks.
The production parser needs neither Rust, Cargo, Python, nor a JSON library.

## Configure the differential gate

CPM fetches and builds the runtime revision in [source evidence](evidence.md).
Configure Git with read access to its repository; no runtime installation is
required.
Set `REFERENCE_SOURCE` to your local `cel-rs` checkout. The accepted reference
starts at `03d658f8b12acabe1d6a2503281e7c86cf00b6a3`; the P4 selector diagnostic
correction is `1516e45f0799927043352ffeeb2b13d58ac66526`. It must also contain
the integer-form float fix `c9a45c479a70ef31c9b7b2d86b9f407d46074370` and the
exponent-sign fix `e1ea1d3083fe97fd26b49352e62c88c24ffa7913` in its `HEAD`
history. `prepare_reference.py` verifies both commits before invoking Cargo.
Use an ordinary C++20 build without fast-math:

```sh
cmake --preset test \
  -DCEL_PARSER_ENABLE_PARITY=ON \
  -DCEL_PARSER_REFERENCE_SOURCE="$REFERENCE_SOURCE"
cmake --build --preset test
ctest --preset test
```

Enabled parity requires `BUILD_TESTING`, Python 3.9 or later, Cargo, source
manifests for `cel-parser` and `cel-runtime`, and the source lockfile.
Missing inputs fail configuration or the reference build; they do not skip a
test. `tools/parity/prepare_reference.py` generates a test-only Cargo project
under the build directory, seeds its initial resolution from the supplied
lockfile, and locks subsequent builds. No personal reference path appears in
shipped manifests or exported package targets.

Windows CI uses the `test` preset's Debug configuration, which enables
[MSVC iterator debugging](https://learn.microsoft.com/en-us/cpp/standard-library/iterator-debug-level)
at its default `_ITERATOR_DEBUG_LEVEL=2`. Tests do not replace global allocation
functions.

The driver uses public parser/runtime APIs. It consumes tuples safely with
`OwnedValue::into_fields` and arrays through checked whole-array typed witnesses.
The Rust public boundary has no arbitrary erased array traversal; the driver
enumerates its deterministic fixture witnesses and fails an unknown witness.
It never inspects private carriers or uses unsafe code. JSON fixture files,
when requested, use Python's standard `json` module; product decoding remains
the client's responsibility. The fixture transport uses one tab-separated
request per line and rejects tabs/line breaks in fixture expressions or setup
names. This tooling restriction does not limit the parser or decoded client;
their public lexer/client contracts cover whitespace and multiline text.

`fixtures.py` maintains unmodified decoded expressions and independently
specified scalar expectations. `core_corpus.py` adds deterministic core grammar,
operator, and lexical-boundary cases. The gate sends every identical source
to Rust and C++, with no source rewriting or accepted C++-only exemptions.
`core` uses fresh intrinsic-only lookups; explicit client setups install matching
callbacks, aliases, records, and field-accessor operations. Every C++ compile
success requires Rust compile success, even when execution subsequently fails.
A documented `cpp_reject` case checks narrower C++ acceptance, not an oracle bypass.
Positive fixtures require successful parsing in both drivers.

Both drivers compile before executing callbacks, destroy the
parser, execute successful fixtures three times, and compare complete semantic
types, structural values, ordered effects, and exact binary64 bits.
Each successful execution's normalized trace is compared with the first,
alongside its type and value; a stable final trace cannot conceal changed
earlier effects. Traced
callback arguments also retain full f64 bits without narrowing to integers. NaN
payloads are unspecified and normalize to `nan`. Signed-zero bits compare
exactly. Callable values compare
their full signature, not native bytes; invocation fixtures and the Rust
typed callable-array test verify real retained code after parser destruction.

The C++ driver checks a caller-owned boolean prefix after execution and
exception cleanup. Rust uses its public safe cached execution API; that API
does not expose the same caller-owned prefix boundary. C++ cached/runtime
contract tests independently verify that boundary rather than fabricate an
equivalent Rust raw-storage test.

## Interpret diagnostics and narrower acceptance

Diagnostic fixtures check their declared C++ categories and byte intervals.
Checked C++ rejections require complete parse-error records; only cases with
`expected_cpp` pin the category and interval. Runtime fixtures check the
execution boundary and trace. Rust has no corresponding category enum, so the
comparison does not invent an exact Rust category.

Only named diagnostic differences and checked C++ rejections are allowed:

- `group-delimiter`: proc-macro group spans include the entire group; C++ closing
  delimiters have their own token spans.
- `eof-sentinel`: an absent Rust token carries call-site `0..0`; the C++ text
  lexer has a real EOF byte position.
- `excluded-parameter-type`: the approved C++ parameter set excludes client
  leaves, arrays of client leaves, and callable parameters that Rust admits.
- Integer value forms and postfix bracket indexing are rejected by the C++ core.
  They are not accepted-language adaptations.

Malformed selector conversion is **not** an EOF adaptation. P4 corrected the
Rust parser to retain the original literal span. Record field type failures
are field-name-local; unknown constructors and incomplete records cover the
record expression. The harness rejects unexpected parse failures, changed
traces/types/results, unknown adapters, missing drivers, and optimized Python
execution that would disable assertion checks.
Before equality or per-row adaptations, it requires complete recognized
response phases: four fields for success/parse errors and three for runtime
errors, with nonempty discriminators and valid parse intervals. Empty traces
remain valid.

Client callback failure normalization uses a distinct Rust error type and
the matching C++ exception type. Only the selected client callback enters this
normalization boundary; serialization and prefix checks propagate their
exceptions. No standard-function or bracket-indexing error adaptation remains.
Unexpected errors still fail the driver. Exception wording is not compared
between languages.

## Package artifact checks

The `cpp-library`-generated package check verifies that the parser component
is present in the install tree. It doesn't validate consumer discovery or
runtime availability; `find_dependency(cel-runtime)` is expected to fail
because CPM excludes the runtime from recursive installation by default. We
don't maintain an installed-consumer test. Address installation failures if a
client reports one.

## Provision CI inputs explicitly

The cpp-library-derived hosted jobs fetch their dependencies through CPM.
Runners need Git read access to the runtime repository, including any
authentication required for a private repository.
The separate manual workflow requires a Rust reference source path, including
`Cargo.lock`, on an already provisioned Unix self-hosted runner.
It runs Debug/Release, negative-compilation tests, parity,
matching sanitizer tests, lint, driver Cargo checks, and strict docs.
It serializes manual dispatches without cancelling an active run and limits
each job to 60 minutes. It does not provision a runner, Git credentials, or the
Rust checkout, or claim Windows coverage.

Local AppleClang results do not establish actual GCC or MSVC results.
macOS LeakSanitizer is unsupported and disabled.
Independent task/final port review remains a separate gate.
