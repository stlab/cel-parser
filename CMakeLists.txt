cmake_minimum_required(VERSION 3.24)

include(cmake/CPM.cmake)

set(CPP_LIBRARY_VERSION "" CACHE STRING "CEL parser package version override")
CPMAddPackage("gh:stlab/cpp-library@5.8.0")
include(${cpp-library_SOURCE_DIR}/cpp-library.cmake)

# Enable dependency tracking before project()
cpp_library_enable_dependency_tracking()

# Now declare project
project(parser LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
include(GNUInstallDirs)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

# Both cel packages share CEL_INSTALL; choose its default before adding dependencies.
option(CEL_INSTALL "Enable installation for cel libraries" ${PROJECT_IS_TOP_LEVEL})
# Keep the dependency eligible for installation; CPM shorthand excludes it.
CPMAddPackage(
    NAME cel-runtime
    GITHUB_REPOSITORY stlab/cel-runtime
    GIT_TAG f6e3d9d1651702f8c2befafa1f842dfd6ec71677
)

# Setup library
cpp_library_setup(
    DESCRIPTION "UTF-8 CEL expression compilation and checked library extensions"
    NAMESPACE cel
    REQUIRES_CPP_VERSION 20
    HEADERS parser.hpp parser/diagnostic.hpp parser/lookup.hpp parser/compiler.hpp parser/all.hpp
    SOURCES parser.cpp lookup.cpp type.cpp compiler.cpp
    EXAMPLES example.cpp cached_decoded.cpp
    TESTS tests.cpp lookup_test.cpp type_test.cpp expression_test.cpp cached_test.cpp parity_driver_test.cpp header_layers_test.cpp
        cursor_copy_fail.cpp lexer_copy_fail.cpp parser_copy_fail.cpp standard_library_fail.cpp
        converting_registration_fail.cpp converting_result_fail.cpp nontrivial_registration_fail.cpp token_interface_fail.cpp
    DOCS_INPUTS README.md docs/build.md docs/get-started.md docs/libraries.md
        docs/language.md docs/reference.dox
    DOCS_OPTIONS "USE_MDFILE_AS_MAINPAGE = ${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "EXCLUDE_SYMBOLS = cel::detail cel::detail::* cel::parser_detail cel::parser_detail::*"
        "FULL_PATH_NAMES = NO"
        "STRIP_FROM_PATH = ${CMAKE_CURRENT_SOURCE_DIR}"
        "STRIP_FROM_INC_PATH = ${CMAKE_CURRENT_SOURCE_DIR}/include"
)
cpp_library_map_dependency("cel::runtime" "cel-runtime CONFIG")
target_link_libraries(parser PUBLIC cel::runtime)
if(TARGET parity_driver_test)
    target_sources(parity_driver_test PRIVATE tools/parity/driver.cpp)
    target_compile_definitions(parity_driver_test PRIVATE CEL_PARITY_NO_MAIN)
endif()
install(FILES LICENSE licenses/Unicode-3.0.txt
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cel-parser")
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
    foreach(test tests lookup_test type_test expression_test parity_driver_test cursor_copy_fail lexer_copy_fail)
        target_include_directories(${test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    endforeach()
    foreach(header parser.hpp parser/diagnostic.hpp parser/lookup.hpp parser/compiler.hpp parser/all.hpp)
        string(MAKE_C_IDENTIFIER "${header}" header_id)
        set(header_source "${CMAKE_CURRENT_BINARY_DIR}/header_${header_id}.cpp")
        file(GENERATE OUTPUT "${header_source}" CONTENT "#include <cel/${header}>\n")
        add_library(header_${header_id} OBJECT "${header_source}")
        target_link_libraries(header_${header_id} PRIVATE cel::parser)
    endforeach()
    configure_file(tests/installed_consumer.cmake.in installed_consumer.cmake @ONLY)
    configure_file(tests/source_consumer.cmake.in source_consumer.cmake @ONLY)
    add_test(NAME installed_consumer
        COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/installed_consumer.cmake")
    add_test(NAME source_consumer
        COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/source_consumer.cmake")
    set_tests_properties(installed_consumer source_consumer tests PROPERTIES TIMEOUT 180)
endif()

option(CEL_PARSER_BUILD_MEASUREMENTS "Build execution timing workloads" OFF)
if(CEL_PARSER_BUILD_MEASUREMENTS)
    add_executable(measurement tools/measurement.cpp)
    target_link_libraries(measurement PRIVATE cel::parser)
    set_target_properties(measurement PROPERTIES CXX_EXTENSIONS OFF)
    if(BUILD_TESTING)
        add_test(NAME measurement_contracts COMMAND measurement 10)
    endif()
endif()

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
    target_include_directories(parity_driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(parity_driver PRIVATE cel::parser)
    set_target_properties(parity_driver PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME parity_comparator COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity" -p "test_*.py")
    add_test(NAME differential COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/harness.py"
        --cpp "$<TARGET_FILE:parity_driver>"
        --rust "${reference_build}/target/release/cel-parity-reference${CMAKE_EXECUTABLE_SUFFIX}")
    set_tests_properties(differential PROPERTIES TIMEOUT 180)
endif()

foreach(tool cached_decoded cached_test measurement parity_driver parity_driver_test)
    if(TARGET ${tool})
        if(MSVC)
            target_compile_options(${tool} PRIVATE /W4 /WX)
        else()
            target_compile_options(${tool} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()
endforeach()
