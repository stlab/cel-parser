# CEL parser

> **Note:** This project is a temporary solution for using the CEL language from
> [cel-rs](https://github.com/stlab/cel-rs) in C++. This library will be deprecated
> when using the Rust implementations from C++ becomes viable.

Compile decoded UTF-8 `std::string_view` expressions once, then execute the
owned programs on a reusable stack. This C++20 library supports a
[bool/f64 subset of CEL](docs/language.md) and lets you add client types and
operations. JSON decoding belongs to your application.

## Build from source with CPM

Use CPM to build the parser and its runtime as static libraries with your
application. After [adding CPM to your project](docs/build.md), declare the
dependency and link your application target:

```cmake
set(CPM_USE_LOCAL_PACKAGES OFF)
include(cmake/CPM.cmake)
CPMAddPackage("gh:stlab/cel-parser@1.0.0")
add_executable(client main.cpp)
target_link_libraries(client PRIVATE cel::parser)
```

The [build tutorial](docs/build.md) includes compiler prerequisites and
configure/build commands. You do not need an installation, package prefix,
or separate runtime checkout.

## Compile your first expression

Include `<cel/parser.hpp>` to compile and execute a scalar:

```cpp
#include <cel/parser.hpp>
#include <iostream>

int main() try {
    cel::parser compiler(cel::op_lookup{});
    auto program = compiler.parse_str("2.0 + 3.0").prepare<double>();
    cel::raw_stack stack;
    std::cout << program.call(stack) << '\n';
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

The program prints `5`. Numeric values require floating syntax: `2.0`, not `2`.
Preparation binds the result type once; later calls reuse the program and stack.

## Follow the tutorials

Read these guides in order:

1. [Build and link the parser with CPM](docs/build.md).
2. [Compile and reuse expressions](docs/get-started.md).
3. [Add an extension library](docs/libraries.md).
4. [Check the supported language](docs/language.md).

The generated documentation places grouped APIs under **Reference** and
symbol lookup under **Index**. Build it with `cmake --preset docs` followed by
`cmake --build --preset docs`, then open `build/docs/html/index.html`.
