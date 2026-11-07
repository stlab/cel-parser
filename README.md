# CEL parser library

This C++20 library gives CEL library developers owned tokens, byte-located
diagnostics, forward-only traversal, complete type parsing, checked operation
lookup, and retained CEL expression compilation. You can register client types
and callbacks, compile source once, and execute cached code after the parser
dies. JSON decoding and Rust/C++ differential verification are outside this library's
P3 milestone.

## Build and verify

Use CMake 3.24 or later, Ninja, and a C++20 compiler with floating-point
`std::from_chars` support. The library requires IEEE binary64 `double` and an
installed `cel-runtime` package exposing checked structural/lazy/callable emission
and `bind_runtime_ownership`
(accepted prerequisite revision `bf2d2ac9fe420139b85d1adec9d57c1565356e32`).
Do not enable fast-math.

Set `RUNTIME_PREFIX` to that package's installation prefix before these commands.
The runtime's hosting location is undecided; this library doesn't invent a
remote acquisition route.

1. Configure the Debug tests:

   ```sh
   cmake --preset=test \
     -Dcel-runtime_DIR="$RUNTIME_PREFIX/lib/cmake/cel-runtime"
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

Install the library with the generated installation preset:

```sh
cmake --preset=install \
  -Dcel-runtime_DIR="$RUNTIME_PREFIX/lib/cmake/cel-runtime"
cmake --build --preset=install
cmake --install build/install --prefix build/prefix
```

Use `find_package(cel-parser CONFIG REQUIRED)` and link `cel::parser`. Supply
both installation prefixes through `CMAKE_PREFIX_PATH`. The parser exports its
public `cel::runtime` dependency and resolves it with `find_dependency`.
Its exported configuration contains no personal or sibling paths.

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
#include <cel/parser.hpp>

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
#include <cel/parser.hpp>

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

## Reference and provenance

See the [lexical contract](docs/lexical-contract.md) for tokens, source ownership,
errors, and adapter differences. See the [lookup and type contract](docs/lookup-contract.md)
for registration, scope selectors, callable names, and type syntax. See the
[built-in census](docs/builtin-census.md) for retained and rejected signatures.
See the [expression grammar](docs/expression-grammar.md) for precedence,
aggregate annotations, explicit closures, and ownership/diagnostic contracts.
See the [source evidence](docs/evidence.md) for
the pinned Rust contracts, Unicode generation, numeric conversion, dependencies,
and remaining parser work.

Generate strict Doxygen documentation with the provided preset:

```sh
cmake --preset=docs \
  -Dcel-runtime_DIR="$RUNTIME_PREFIX/lib/cmake/cel-runtime"
cmake --build --preset=docs
```

The generated CI describes GCC, Clang, macOS, and Windows checks. A generated
workflow isn't evidence that those jobs have executed. Its runners must receive
an installed runtime package before configuring this parser.
