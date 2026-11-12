# Add an extension library {#parser_libraries}

This tutorial installs a typed operation in `cel::op_lookup`, compiles a call,
and executes it after the compiler is destroyed. Start with
[Build and link the parser with CPM](build.md).

## Register a receiver operation

Register operations before transferring the lookup into a compiler. The
template arguments list the result first, followed by source-ordered inputs:

```cpp
#include <cel/parser.hpp>
#include <iostream>

void install_numbers(cel::op_lookup& lookup) {
    lookup.register_op<double, double>(
        "twice", [](double x) { return x * 2.0; });
}

int main() try {
    auto program = [] {
        cel::op_lookup lookup;
        install_numbers(lookup);
        cel::parser compiler(std::move(lookup));
        return compiler.parse_str("3.0.twice()").prepare<double>();
    }();
    cel::raw_stack stack;
    double result = program.call(stack); // 6.0
    std::cout << result << '\n';
    return result == 6.0 ? 0 : 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

Replace `main.cpp` in the CPM build tutorial with this program. Rebuild and
run the same CTest command; confirm that it passes and prints `6`.

The receiver is the first input, so `"twice"` accepts exactly one `double`.
Registrations match complete types; return types do not distinguish overloads.
Duplicate input signatures are rejected. The callback runs during execution,
and the compiled program retains it independently of the lookup.

Your library can publish an installer such as `install_numbers`. Its
registration-only header can include `<cel/parser/lookup.hpp>` without the
compiler header. Store captures by value when the program must own them.
Captured references must outlive every execution that uses them.

## Name an ordinary function

An ordinary name needs a marker binding in addition to its operation overload:

```cpp
struct identity_marker {};
cel::op_lookup lookup;
lookup.register_function("identity", identity_marker{});
lookup.register_op<double, double>(
    "identity", [](double x) { return x; });
cel::parser compiler(std::move(lookup));
auto program = compiler.parse_str("identity(4.0)").prepare<double>();
cel::raw_stack stack;
double result = program.call(stack); // 4.0
```

The marker supplies the named function value; it is not an extra input in the
`register_op` signature. A marker alone supplies no operation.

## Register types and schemas

Use `register_type<T>("Name")` for a trivially copyable opaque client leaf.
This registers the type's identity, not constructors, operators, or ownership
of resources reached through pointers inside it. Register the operations that
produce and consume it separately.

Use `register_record` with named `cel::record_field` descriptors for a nominal
schema. `Name{field: expression}` then constructs that schema, and `.field`
selects a field. Names and complete descriptors are retained; equal layouts or
display names do not make separately identified types compatible.

## Choose a lower-level extension only when needed

Use `register_operation` for a complete descriptor signature and a checked
`cel::dyn_segment` emitter. It must consume the declared operands and leave one
compatible result. The compiler checks the emitted depth and result after
selection. `register_structural_op` supplies a lower-priority structural
signature. Client registrations precede core built-ins.

Use `push_library_scope` for persistent name resolution visible inside
independent closures. A scope that returns `false` must leave the builder
unchanged. Successful callbacks and emitters must retain everything their
emitted code needs; captures in the compiler alone do not keep that state alive.

The core supplies no standard-library functions. Check
[the language subset](language.md) before assuming a name or operator has a
built-in implementation.
