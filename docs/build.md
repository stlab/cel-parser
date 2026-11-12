# Build and link the parser with CPM {#parser_build}

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
CPMAddPackage(
    NAME cel-parser
    GITHUB_REPOSITORY stlab/cel-parser
    GIT_TAG 04094fb73a53f9383ce4ea7b78cfb57df9ac7b06
    OPTIONS "BUILD_SHARED_LIBS OFF" "BUILD_TESTING OFF" "CEL_INSTALL OFF"
)
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
source. `BUILD_SHARED_LIBS OFF` selects static libraries, `BUILD_TESTING OFF`
disables dependency tests, and `CEL_INSTALL OFF` disables CEL package
installation. `CPM_USE_LOCAL_PACKAGES OFF` keeps CPM from preferring installed
packages. No install command is part of this build.

The `cel::parser` target supplies the C++20 requirement and links the static
`cel::runtime` dependency. Do not add dependency include or library directories
manually. Keep a reviewed commit in `GIT_TAG` when updating the dependency;
the parser selects its own pinned runtime revision.

Continue with [Compile and reuse expressions](get-started.md) to cache programs
after their source strings and compiler are destroyed.
