# CEL parser library

This C++20 library gives CEL library developers owned tokens, byte-located
diagnostics, forward-only traversal, complete type parsing, checked operation
lookup, and retained CEL expression compilation. You can register client types
and callbacks, compile source once, and execute cached code after the parser
dies. Separate test tooling compares retained contracts with public Rust interfaces.
JSON decoding remains your application's responsibility.

## Build and verify

Use CMake 3.24 or later, Ninja, and a C++20 compiler with floating-point
`std::from_chars` support and IEEE binary64 `double`. Do not enable fast-math.
CPM fetches the pinned cpp-library and `cel-runtime` sources and builds the
runtime with the parser. Test builds also fetch doctest.

The runtime upstream is
[stlab/cel-runtime](https://github.com/stlab/cel-runtime).
Configure Git with read access to that repository before running CMake.
No runtime installation or package prefix is required. See
[source evidence](docs/evidence.md) for the pinned revision.

1. Configure the Debug tests:

   ```sh
   cmake --preset=test
   ```

2. Build the library, example, and tests:

   ```sh
   cmake --build --preset=test
   ```

3. Run unit tests, generated compile-fail tests, and the installed consumer:

   ```sh
   ctest --preset=test
   ```

Confirm that all CTests pass. The compile-fail tests reject cloned traversal,
copied compilers, implicitly converting callbacks, and nontrivial opaque leaves.
The installed consumer checks parser/runtime imported targets and expression
compilation in a fresh prefix.

## Consume the package

Install both libraries into one prefix with the generated installation preset:

```sh
cmake --preset=install
cmake --build --preset=install
cmake --install build/install --prefix build/prefix
```

Use `find_package(cel-parser CONFIG REQUIRED)` and link `cel::parser`. Supply
the installation prefix through `CMAKE_PREFIX_PATH`. The parser exports its
public `cel::runtime` dependency and resolves it with `find_dependency`.
Its exported configuration contains no personal or sibling paths.

Use `<cel/parser.hpp>` for tokens and traversal, `<cel/parser/lookup.hpp>` for
lookup and types, and `<cel/parser/compiler.hpp>` for expression compilation.
The explicit full-stack umbrella is `<cel/parser/all.hpp>`.

Read one scalar with the public header:

```cpp
#include <cel/parser.hpp>

int main() try {
    cel::lex_lexer lexer("1f64");
    if (std::get<cel::numeric_literal>(lexer.current().value).value != 1.0)
        return 1;
    lexer.advance();
    return std::holds_alternative<cel::end_token>(lexer.current().value)
               ? 0 : 1;
} catch (const std::exception&) {
    return 1;
}
```

The installed-consumer CTest compiles and runs this interface without checkout
include paths. It also executes cached callbacks, ordinary standard-function
values, nested composites, and complete type parsing through installed headers.

Compile a callback through checked lookup:

```cpp
#include <cel/parser/lookup.hpp>

int main() try {
    cel::dyn_segment builder;
    {
        cel::op_lookup lookup;
        lookup.register_op<double, double>(
            "twice", [](double x) { return x * 2.0; });
        builder.just(cel::value::from(3.0));
        lookup.apply("twice", builder, 1, {0, 5});
    }
    auto prepared = std::move(builder).finish().prepare<double>();
    cel::raw_stack stack;
    return prepared.call(stack) == 6.0 ? 0 : 1;
} catch (const std::exception&) {
    return 1;
}
```

The result is 6.0. Captured values survive lookup destruction; captured references
remain your lifetime responsibility.

## Compile source expressions

Compile decoded text and execute the cached program on your reusable stack:

```cpp
#include <cel/parser/all.hpp>

int main() try {
    auto code = [] {
        cel::parser compiler(cel::op_lookup{});
        return compiler.parse_str("(|| 7)()");
    }();
    cel::raw_stack stack;
    return code.execute(stack, {}).as<double>() == 7.0 ? 0 : 1;
} catch (const std::exception&) {
    return 1;
}
```

The result is 7.0; the compiler has already been destroyed. Text parsing uses
the owned lexer directly, not a second token vector. `parse_tokens` compiles
a complete borrowed flattened stream; `parse_expression(token_cursor&)`
compiles a prefix and leaves its following host token. Full methods reject
trailing input. EOF/error tokens terminate an external producer stream.

CEL groups opened inside an expression must close with the same visible kind.
Inline parsing doesn't consume or validate its surrounding host group;
external invisible groups are unsupported expression syntax. Syntax/type
errors carry owned byte spans. Selected client-emitter and execution exceptions
propagate without becoming syntax diagnostics. Recursive expression nesting is
limited to 128 active expression/conditional productions; flat operator,
postfix, and list chains are iterative.

## Cache decoded expression strings

Decode your JSON transport before calling the parser. The runnable
[`cached_decoded`](examples/cached_decoded.cpp) client receives three decoded
strings, prepares each scalar result once, destroys the compiler, reserves
one prefixed stack, and repeatedly calls the prepared handles:

```cpp
std::array<std::string_view, 3> decoded{"2+3", "10-4", "sqrt(81)"};
auto programs = client::compile_decoded(decoded);
cel::raw_stack stack;
stack.push(true);
client::reserve(programs, stack);
double sum = 0;
for (int repeat = 0; repeat != 1000; ++repeat)
    for (const auto& program : programs)
        sum += program.call(stack);
```

The example-local helpers in
[`cached_decoded.hpp`](examples/cached_decoded.hpp) use only installed public
interfaces; they are not additional library APIs. The compiler retains no
borrow of the decoded strings. The complete runnable example checks the sum
and preserved prefix. Run it with `build/test/cached_decoded`.

Preparation avoids repeated result-type matching. Reservation covers known
stack requirements, not arbitrary callback allocations or composite payloads.
Named calls pack and retire an owning marker/result tuple even
when their result is scalar. Composite returns and dynamic invocation have
separate ownership/staging costs. See [validation and measurement](docs/validation.md).

## Reference and provenance

See the [lexical contract](docs/lexical-contract.md) for tokens, source ownership,
errors, and adapter differences. See the [lookup and type contract](docs/lookup-contract.md)
for registration, scope selectors, callable names, and type syntax. See the
[built-in census](docs/builtin-census.md) for retained and rejected signatures.
See the [expression grammar](docs/expression-grammar.md) for precedence,
aggregate annotations, explicit closures, and ownership/diagnostic contracts.
See the [source evidence](docs/evidence.md) for
the pinned Rust contracts, Unicode generation, numeric conversion, dependencies,
and validation boundaries.
See [the P4 handoff](docs/p4-handoff.md) for fixture closure and review/platform boundaries.

Generate strict Doxygen documentation with the provided preset:

```sh
cmake --preset=docs
cmake --build --preset=docs
```

The cpp-library-derived hosted matrix uses the same CPM source dependencies.
If the runtime repository is private, configure runner Git authentication with
read access to it; source acquisition does not grant repository access.
The manual `provisioned-validation.yml` additionally requires a local Rust
reference checkout on a self-hosted runner.
It serializes dispatches without interrupting an active run and bounds each job
to 60 minutes. CPM builds the runtime with matching flags, including sanitizers.
The workflow does not provision a runner, Git credentials, or the Rust checkout.
The default build does not silently attempt parity; enable the fail-closed
local gate explicitly with the commands in [validation](docs/validation.md).
