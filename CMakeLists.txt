cmake_minimum_required(VERSION 3.24)

include(cmake/CPM.cmake)

CPMAddPackage("gh:stlab/cpp-library@6.2.0")
include(${cpp-library_SOURCE_DIR}/cpp-library.cmake)

# Enable dependency tracking before project()
cpp_library_enable_dependency_tracking()

# Now declare project
project(parser LANGUAGES CXX)

include(CTest)

# Setup library
cpp_library_setup(
    DESCRIPTION "UTF-8 CEL expression compilation and checked library extensions"
    NAMESPACE cel
    HEADERS parser.hpp parser/diagnostic.hpp parser/lookup.hpp parser/compiler.hpp parser/all.hpp
    SOURCES parser.cpp lookup.cpp type.cpp compiler.cpp
    EXAMPLES example.cpp cached_decoded.cpp
    TESTS lookup_test.cpp expression_test.cpp cached_test.cpp parity_driver_test.cpp header_layers_test.cpp
        cursor_copy_fail.cpp lexer_copy_fail.cpp parser_copy_fail.cpp standard_library_fail.cpp
        converting_registration_fail.cpp converting_result_fail.cpp nontrivial_registration_fail.cpp token_interface_fail.cpp
    LICENSE_FILES licenses/Unicode-3.0.txt
    DOCS_INPUTS README.md docs/build.md docs/get-started.md docs/libraries.md
        docs/language.md docs/reference.dox
    DOCS_EXCLUDE_SYMBOLS cel::detail cel::detail::* cel::parser_detail cel::parser_detail::*
    DOCS_OPTIONS "USE_MDFILE_AS_MAINPAGE = ${CMAKE_CURRENT_SOURCE_DIR}/README.md"
)

# cpp-library adds src to tests directly and through the static implementation.
get_property(parser_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(parser_target IN LISTS parser_targets)
    get_target_property(parser_includes "${parser_target}" INCLUDE_DIRECTORIES)
    if(parser_includes)
        list(REMOVE_ITEM parser_includes "${CMAKE_CURRENT_SOURCE_DIR}/src")
        set_property(TARGET "${parser_target}" PROPERTY INCLUDE_DIRECTORIES "${parser_includes}")
    endif()
endforeach()

# Initialize cpp-library's default CEL_INSTALL before the same-namespace runtime.
CPMAddPackage("gh:stlab/cel-runtime@1.1.3#fc9fa3be8b0af9782a3491acacd6ba1f2e528a5b")
target_link_libraries(parser PUBLIC cel::runtime)

# Exercise parity-driver helpers in ordinary unit tests without the driver's main().
if(TARGET parity_driver_test)
    target_sources(parity_driver_test PRIVATE tools/parity/driver.cpp)
    target_compile_definitions(parity_driver_test PRIVATE CEL_PARITY_NO_MAIN)
endif()

# Compare C++ and Rust results on shared fixtures; Python and Cargo are test-only.
option(CEL_PARSER_ENABLE_PARITY "Require the local public Rust reference differential gate" OFF)
set(CEL_PARSER_REFERENCE_SOURCE "" CACHE PATH "Local cel-rs checkout for test-only parity")
if(CEL_PARSER_ENABLE_PARITY)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Parity requires BUILD_TESTING")
    endif()
    foreach(package cel-parser cel-runtime)
        if(NOT EXISTS "${CEL_PARSER_REFERENCE_SOURCE}/${package}/Cargo.toml")
            message(FATAL_ERROR "Enabled parity requires CEL_PARSER_REFERENCE_SOURCE with ${package}")
        endif()
    endforeach()
    if(NOT EXISTS "${CEL_PARSER_REFERENCE_SOURCE}/Cargo.lock")
        message(FATAL_ERROR "Enabled parity requires the Rust reference Cargo.lock")
    endif()
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    find_program(CEL_PARSER_CARGO cargo REQUIRED)
    set(reference_build "${CMAKE_CURRENT_BINARY_DIR}/parity-reference")
    add_custom_target(parity_reference ALL
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/prepare_reference.py"
            --source "${CEL_PARSER_REFERENCE_SOURCE}" --build "${reference_build}"
        VERBATIM)
    add_executable(parity_driver tools/parity/driver.cpp)
    target_link_libraries(parity_driver PRIVATE cel::parser)
    cpp_library_copy_runtime_dlls(parity_driver)

    # Run Python tooling tests as well as end-to-end C++/Rust comparisons.
    add_test(NAME parity_comparator COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity" -p "test_*.py")
    add_test(NAME differential COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/harness.py"
        --cpp "$<TARGET_FILE:parity_driver>"
        --rust "${reference_build}/target/release/cel-parity-reference${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(differential PROPERTIES TIMEOUT 180)
endif()
