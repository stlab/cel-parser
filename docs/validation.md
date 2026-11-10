# Validate retained contracts and execution costs {#parser_validation}

This guide serves library maintainers. It separates public cross-language
comparisons, C++ adaptations, installed consumption, and local measurements.
The production parser needs neither Rust, Cargo, Python, nor a JSON library.

## Configure the differential gate

CPM fetches and builds the runtime revision in [source evidence](evidence.md).
Configure Git with read access to its repository; no runtime installation is
required.
Set `REFERENCE_SOURCE` to your local `cel-rs` checkout. The accepted reference
starts at `03d658f8b12acabe1d6a2503281e7c86cf00b6a3`; the P4 selector diagnostic
correction is `1516e45f0799927043352ffeeb2b13d58ac66526`.
Use an ordinary C++20 build without fast-math:

```sh
cmake --preset test \
  -DCEL_PARSER_ENABLE_PARITY=ON \
  -DCEL_PARSER_REFERENCE_SOURCE="$REFERENCE_SOURCE" \
  -DCEL_PARSER_BUILD_MEASUREMENTS=ON
cmake --build --preset test
ctest --preset test
```

Enabled parity requires `BUILD_TESTING`, Python 3.9 or later, Cargo, source
manifests for `cel-parser`, `cel-runtime`, and `cel-std`, and the source lockfile.
Missing inputs fail configuration or the reference build; they do not skip a
test. `tools/parity/prepare_reference.py` generates a test-only Cargo project
under the build directory, seeds its initial resolution from the supplied
lockfile, and locks subsequent builds. No personal reference path appears in
shipped manifests or exported package targets.

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

`fixtures.py` maintains decoded expressions and independently specified scalar
expectations. Both drivers compile before executing callbacks, destroy the
parser, execute successful fixtures three times, and compare complete semantic
types, structural values, ordered effects, and exact binary64 bits.
Each successful execution's normalized trace is compared with the first,
alongside its type and value; a stable final trace cannot conceal changed
earlier effects. Traced
callback arguments also retain full f64 bits without narrowing to integers. NaN
payloads are unspecified and normalize to `nan`. Only explicitly marked
`min`/`max` equal-zero fixtures admit either zero sign. Callable values compare
their full signature, not native bytes; invocation fixtures and the Rust
typed callable-array test verify real retained code after parser destruction.

The C++ driver checks a caller-owned boolean prefix after execution and
exception cleanup. Rust uses its public safe cached execution API; that API
does not expose the same caller-owned prefix boundary. C++ cached/runtime
contract tests independently verify that boundary rather than fabricate an
equivalent Rust raw-storage test.

## Interpret diagnostics and adaptations

Every failure fixture checks the C++ category and both byte intervals. Rust
has no corresponding category enum, so the comparison checks its parse/runtime
boundary rather than inventing an exact Rust category.

Only named per-fixture adaptations are allowed:

- `group-delimiter`: proc-macro group spans include the entire group; C++ closing
  delimiters have their own token spans.
- `eof-sentinel`: an absent Rust token carries call-site `0..0`; the C++ text
  lexer has a real EOF byte position.
- `excluded-parameter-type`: the approved C++ parameter set excludes client
  leaves, arrays of client leaves, and callable parameters that Rust admits.
- Integer-spelled binary64 literals and checked bracket indexing follow
  explicit C++ contracts, not Rust syntax equality.

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
the matching C++ exception type. Rust's standard clamp emitter returns an
untyped `anyhow` error; only explicitly selected clamp fixtures with its exact
source-defined `"invalid clamp bounds"` error normalize to `runtime/clamp`.
C++ requires the same clamp witness and recognizes only the selected indexing
witnesses `"invalid floating array index"` and `"array index out of bounds"`.
Only execution enters this normalization boundary; serialization and prefix
checks propagate their exceptions.
Unexpected errors still fail the driver. Exception wording is not compared
between languages.

## Verify installed consumption

`installed_consumer` installs parser and runtime output from the same build tree
into an isolated prefix. It rejects leaked checkout/sibling paths
and verifies both imported libraries and headers resolve to that prefix.
Five package-only clients cover tokens, lookup, compiled composites/callables,
the decoded scalar cache, and execution measurements. They link only
`cel::parser`; `find_dependency` finds the runtime transitively.
The runtime shares the parser's compiler and instrumentation flags.
To check both libraries and installed clients under ASan+UBSan:

```sh
cmake -S . -B build/parser-sanitized -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" \
  -DCEL_PARSER_ENABLE_PARITY=ON \
  "-DCEL_PARSER_REFERENCE_SOURCE=$REFERENCE_SOURCE" \
  -DCEL_PARSER_BUILD_MEASUREMENTS=ON
cmake --build build/parser-sanitized
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/parser-sanitized --output-on-failure
```

`detect_leaks=0` is required on macOS, which does not support LeakSanitizer.
On a platform with supported LeakSanitizer, omit that override to retain leak
detection. Confirm that parser tests and installed clients pass.

Set `COMBINED_PREFIX` to the absolute installation prefix containing both
libraries. To repeat with packages you installed yourself:

```sh
cmake -S tests/installed_consumer -B build/package-client -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$COMBINED_PREFIX"
cmake --build build/package-client
ctest --test-dir build/package-client --output-on-failure
```

No checkout include directory or explicit runtime target is required.

## Measure optimized warmed execution

Configure Release with the Rust reference checkout and
`CEL_PARSER_BUILD_MEASUREMENTS=ON`, then run:

```sh
build/release/measurement 100000
```

The CSV reports workload, iterations, total nanoseconds, allocation calls,
and observable result sum. Each workload has 100 warmups, then separate
allocation and monotonic-clock timing passes. Counts cover ordinary/aligned
scalar/array allocation functions; they do not measure bytes or leaks.
The stack reserves 4096 bytes aligned to 64 and 256 live slots over a bool prefix.
Compilation, preparation, factory creation, and reservation occur before counts.
No timing threshold appears in unit tests.

| Workload | Boundary |
| --- | --- |
| `prepared-scalar` | `2+3*4+(81).sqrt()`; direct arithmetic/member dispatch, typed double result |
| `named-scalar-aggregate-retirement` | `2+3*4+sqrt(81)`; owning intermediate marker/result packing |
| `prepared-owning-nested-array` | `[[1,2],[3,4]]`; native nested-vector result conversion and retirement |
| `erased-owning-nested-array` | Same source; public erased owning result, no child inspection |
| `dynamic-callable` | `\|x:f64\|x*2+1`; shared callable, boxed argument 2, public dynamic invocation |

The scalar check requires zero warmed allocation calls for that direct workload.
It makes no zero-allocation promise for all scalar-result source expressions.
Keep platform/build-specific measurements in the handoff/report, not portable
speed claims. The measurement-only clang-tidy target excludes
`clang-analyzer-unix.cstring.NullArg`: that checker invents a nonempty f64 array
whose canonical element storage is null inside the installed native adapter.
The public canonical array contract excludes that path. All other checks stay
enabled; runtime production headers are unchanged and ASan+UBSan/contract tests
exercise actual owning conversion.

## Provision CI inputs explicitly

The cpp-library-derived hosted jobs fetch their dependencies through CPM.
Runners need Git read access to the runtime repository, including any
authentication required for a private repository.
The separate manual workflow requires a Rust reference source path, including
`Cargo.lock`, on an already provisioned Unix self-hosted runner.
It runs Debug/Release, negative-compilation tests, installed clients, parity,
measurements, matching sanitizer tests, lint, driver Cargo checks, and strict docs.
It serializes manual dispatches without cancelling an active run and limits
each job to 60 minutes. It does not provision a runner, Git credentials, or the
Rust checkout, or claim Windows coverage.

Local AppleClang results do not establish actual GCC or MSVC results.
macOS LeakSanitizer is unsupported and disabled; allocation counters are not
leak detection. Independent task/final port review remains a separate gate.
