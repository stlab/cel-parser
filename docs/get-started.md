# Compile and reuse expressions {#parser_get_started}

This tutorial compiles decoded text into cached programs, extracts results,
and reuses caller-owned stack storage. Start with
[Build and link the parser with CPM](build.md).

## Compile once and extract a result

`parse_str` consumes the complete UTF-8 string and returns an owned
`cel::compiled_segment`. This program remains valid after both its compiler
and its source string disappear:

```cpp
#include <cel/parser.hpp>
#include <string>

auto code = [] {
    cel::parser compiler(cel::op_lookup{});
    std::string source = "(|| 7.0)()";
    return compiler.parse_str(source);
}();
cel::raw_stack stack;
auto result = code.execute(stack, {});
double number = result.as<double>(); // 7.0
```

`execute` takes the stack and a span of owning argument values; this program
needs none. The returned `cel::value` owns its result independently of the
execution frame. Arrays and tuples can be extracted using the runtime's
`std::vector` and `std::tuple` adapters. Use `at()` and `field()` when you need
to inspect a result whose concrete shape is determined at runtime.

For a known result type, prepare the program once and call its typed boundary:

```cpp
auto prepared = code.prepare<double>();
double first = prepared.call(stack);  // 7.0
double second = prepared.call(stack); // 7.0
```

Preparation checks the complete result signature. Calls return independent
results and retire their temporary stack suffix. The stack remains reusable
after successful calls and exceptions; callback side effects are not rolled
back. Use one exclusive evaluator per stack, and synchronize any shared mutable
callback state separately.

## Run a cache of decoded strings

The cache example compiles three expressions, destroys the compiler,
reserves storage around a caller-owned bool prefix, and evaluates each
prepared program 1,000 times:

\include cached_decoded.cpp

The example-local helpers use only public interfaces. `compile_decoded`
prepares every result; `reserve` reserves each program's requirements without
shrinking existing capacity:

\include cached_decoded.hpp

The strings are already decoded UTF-8. Decode JSON or another transport
format before passing its string values to `parse_str`; the compiler does not
decode transport escapes.

Use the application from the CPM build tutorial to run this example:

1. Replace its `add_executable` line with the example source fetched by CPM:

   ```cmake
   add_executable(client "${cel-parser_SOURCE_DIR}/examples/cached_decoded.cpp")
   ```

2. Build it:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

3. Run the cache test:

   ```sh
   ctest --test-dir build --output-on-failure --verbose
   ```

   Confirm that the test passes and prints `cached sum=30000`. The test also
   checks that the bool prefix survives every call.

Measure the stack cursor and depth before reserving, without intervening
prefix mutation. Reserve before entering evaluation. Known requirements cover
the program's canonical stack storage, not composite payload allocations,
arbitrary callback work, or unknown external callable bodies.

## Handle compilation errors

Catch `cel::parse_error` to obtain a diagnostic category and a half-open source
interval from `span().begin` through `span().end`. Offsets count UTF-8 bytes,
not characters. Retain the source yourself if you want to display the
diagnostic against it.

Malformed input, unsupported literals, incompatible types, missing operations,
delimiter errors, and nesting limits have distinct categories. A failed parse
normally leaves the compiler reusable. Client emitters run during compilation;
their exceptions propagate without being relabeled as syntax errors. Emitted
runtime callbacks execute only when you call the program.

Continue with [Add an extension library](libraries.md) to compile named client
operations.
