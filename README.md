# CEL parser token boundary

This C++20 library gives CEL library developers owned tokens, byte-located
diagnostics, and forward-only traversal of decoded expression text. It doesn't
compile expressions, resolve operations, or decode JSON.

## Build and verify

Use CMake 3.24 or later, Ninja, and a C++20 compiler with floating-point
`std::from_chars` support. The library requires IEEE binary64 `double`.

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

Confirm that all five CTests pass. The compile-fail tests verify that traversal
state cannot be cloned.

## Consume the package

Install the library with the generated installation preset:

```sh
cmake --preset=install
cmake --build --preset=install
cmake --install build/install --prefix build/prefix
```

Use `find_package(cel-parser CONFIG REQUIRED)` and link `cel::parser`. Supply
the installation prefix through `CMAKE_PREFIX_PATH`. The token-only milestone
has no runtime dependency; its exported package contains no sibling paths.

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
include paths.

## Reference and provenance

See the [lexical contract](docs/lexical-contract.md) for tokens, source ownership,
errors, and adapter differences. See the [source evidence](docs/evidence.md) for
the pinned Rust contracts, Unicode generation, numeric conversion, dependencies,
and remaining parser work.

Generate strict Doxygen documentation with the provided preset:

```sh
cmake --preset=docs
cmake --build --preset=docs
```

The generated CI describes GCC, Clang, macOS, and Windows checks. A generated
workflow isn't evidence that those jobs have executed.
