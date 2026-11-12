# Build and link the parser with CPM {#parser_build}

> **Note:** This project is a temporary solution for using the CEL language from
> [cel-rs](https://github.com/stlab/cel-rs) in C++. This library will be deprecated
> when using the Rust implementations from C++ becomes viable.

This tutorial builds the parser and its runtime from source as static
libraries inside your application's CMake build. No installation or package
prefix is needed.

## Before you begin

Use CMake 3.24 or later, Ninja, Git, and a C++20 compiler with floating-point
`std::from_chars` and IEEE binary64 `double`. Do not enable fast-math.
The first configuration downloads pinned build infrastructure and
`cel-runtime`. You need Git read access to both repositories; no separate
runtime checkout is required.
The commands below use a POSIX shell.

## Add CPM to your application

1. Create an application directory:

   ```sh
   mkdir parser-client
   cd parser-client
   ```

2. Create a directory for the CPM bootstrap:

   ```sh
   mkdir -p cmake
   ```

3. Save this bootstrap as `cmake/CPM.cmake`:

   ```cmake
   file(DOWNLOAD
       https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.2/CPM.cmake
       "${CMAKE_BINARY_DIR}/cmake/CPM_0.43.2.cmake"
       EXPECTED_HASH
           SHA256=49a3bef91ceb65bb66d57255e12d1ffd22abc2f6408fa9fe4534c544a2f232aa
       TLS_VERIFY ON
   )
   include("${CMAKE_BINARY_DIR}/cmake/CPM_0.43.2.cmake")
   ```

   CMake downloads and verifies CPM before executing it. A checksum mismatch
   stops configuration. Keep the version and checksum together when updating.

## Build the parser with your application

Save the complete first-expression program from the [main page](../README.md)
as `main.cpp` in your application directory. Create `CMakeLists.txt` beside it:

```cmake
cmake_minimum_required(VERSION 3.24)
project(parser_client LANGUAGES CXX)
set(CPM_USE_LOCAL_PACKAGES OFF)
include(cmake/CPM.cmake)
CPMAddPackage("gh:stlab/cel-parser@1.0.0")
add_executable(client main.cpp)
target_link_libraries(client PRIVATE cel::parser)
enable_testing()
add_test(NAME client COMMAND client)
```

Configure, build, and run from your application directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure --verbose
```

Confirm that the test passes and prints `5`. CPM builds both libraries from
source as static libraries by default. Dependency builds omit the libraries'
tests and CEL package installation rules.
`CPM_USE_LOCAL_PACKAGES OFF` keeps CPM from preferring installed packages.
No install command is part of this build.

The `cel::parser` target supplies the C++20 requirement and links the static
`cel::runtime` dependency. Do not add dependency include or library directories
manually. Select a reviewed release after `@` when updating the dependency;
the parser selects its own pinned runtime revision.

Continue with [Compile and reuse expressions](get-started.md) to cache programs
after their source strings and compiler are destroyed.
