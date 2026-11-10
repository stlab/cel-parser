cmake_minimum_required(VERSION 3.24)

include(cmake/CPM.cmake)

set(CPP_LIBRARY_VERSION "0.0.0" CACHE STRING "CEL parser package version")
CPMAddPackage("gh:stlab/cpp-library#b0b096964a93561c7dd1586fa7b0efdf5f077aab")
include(${cpp-library_SOURCE_DIR}/cpp-library.cmake)

# Enable dependency tracking before project()
cpp_library_enable_dependency_tracking()

# Now declare project
project(parser LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

# Enable CTest infrastructure (required for tests/examples to work)
include(CTest)
find_package(cel-runtime CONFIG REQUIRED)

# Setup library
cpp_library_setup(
    DESCRIPTION "CEL expression compilation, owned tokens, and checked operation lookup"
    NAMESPACE cel
    REQUIRES_CPP_VERSION 20
    HEADERS parser.hpp parser/lookup.hpp parser/compiler.hpp parser/all.hpp
    SOURCES parser.cpp lookup.cpp type.cpp compiler.cpp
    EXAMPLES example.cpp cached_decoded.cpp
    TESTS tests.cpp lookup_test.cpp type_test.cpp allocation_test.cpp expression_test.cpp cached_test.cpp parity_driver_test.cpp header_layers_test.cpp
        cursor_copy_fail.cpp lexer_copy_fail.cpp parser_copy_fail.cpp
        converting_registration_fail.cpp converting_result_fail.cpp nontrivial_registration_fail.cpp
    DOCS_INPUTS README.md docs/lexical-contract.md docs/evidence.md
        docs/lookup-contract.md docs/builtin-census.md docs/expression-grammar.md
        docs/validation.md docs/p4-handoff.md
    DOCS_OPTIONS "USE_MDFILE_AS_MAINPAGE = ${CMAKE_CURRENT_SOURCE_DIR}/README.md"
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
if(BUILD_TESTING)
    foreach(header parser.hpp parser/lookup.hpp parser/compiler.hpp parser/all.hpp)
        string(MAKE_C_IDENTIFIER "${header}" header_id)
        set(header_source "${CMAKE_CURRENT_BINARY_DIR}/header_${header_id}.cpp")
        file(GENERATE OUTPUT "${header_source}" CONTENT "#include <cel/${header}>\n")
        add_library(header_${header_id} OBJECT "${header_source}")
        target_link_libraries(header_${header_id} PRIVATE cel::parser)
    endforeach()
    get_target_property(runtime_includes cel::runtime INTERFACE_INCLUDE_DIRECTORIES)
    list(GET runtime_includes 0 runtime_include)
    get_filename_component(CEL_PARSER_RUNTIME_PREFIX "${runtime_include}" DIRECTORY)
    configure_file(tests/installed_consumer.cmake.in installed_consumer.cmake @ONLY)
    add_test(NAME installed_consumer
        COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/installed_consumer.cmake")
    set_tests_properties(installed_consumer tests PROPERTIES TIMEOUT 180)
endif()

option(CEL_PARSER_BUILD_MEASUREMENTS "Build separate allocation and timing workloads" OFF)
if(CEL_PARSER_BUILD_MEASUREMENTS)
    add_executable(measurement tools/measurement.cpp)
    target_link_libraries(measurement PRIVATE cel::parser)
    set_target_properties(measurement PROPERTIES CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_CLANG_TIDY)
        # The analyzer invents a nonempty f64 array with null element storage in the
        # installed adapter. Valid canonical arrays cannot satisfy that path.
        set_property(TARGET measurement APPEND PROPERTY CXX_CLANG_TIDY
            "--checks=-clang-analyzer-unix.cstring.NullArg")
    endif()
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
    foreach(package cel-parser cel-runtime cel-std)
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
    set_target_properties(parity_driver PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME parity_comparator COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/parity/test_harness.py")
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
