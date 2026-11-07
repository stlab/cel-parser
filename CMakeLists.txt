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
    HEADERS parser.hpp parser/lookup.hpp parser/compiler.hpp
    SOURCES parser.cpp lookup.cpp type.cpp compiler.cpp
    EXAMPLES example.cpp
    TESTS tests.cpp lookup_test.cpp type_test.cpp allocation_test.cpp expression_test.cpp
        cursor_copy_fail.cpp lexer_copy_fail.cpp parser_copy_fail.cpp
        converting_registration_fail.cpp converting_result_fail.cpp nontrivial_registration_fail.cpp
    DOCS_INPUTS README.md docs/lexical-contract.md docs/evidence.md
        docs/lookup-contract.md docs/builtin-census.md docs/expression-grammar.md
    DOCS_OPTIONS "USE_MDFILE_AS_MAINPAGE = ${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "FULL_PATH_NAMES = NO"
        "STRIP_FROM_PATH = ${CMAKE_CURRENT_SOURCE_DIR}"
        "STRIP_FROM_INC_PATH = ${CMAKE_CURRENT_SOURCE_DIR}/include"
)
cpp_library_map_dependency("cel::runtime" "cel-runtime CONFIG")
target_link_libraries(parser PUBLIC cel::runtime)
install(FILES LICENSE licenses/Unicode-3.0.txt
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cel-parser")
if(BUILD_TESTING)
    get_target_property(runtime_includes cel::runtime INTERFACE_INCLUDE_DIRECTORIES)
    list(GET runtime_includes 0 runtime_include)
    get_filename_component(CEL_PARSER_RUNTIME_PREFIX "${runtime_include}" DIRECTORY)
    configure_file(tests/installed_consumer.cmake.in installed_consumer.cmake @ONLY)
    add_test(NAME installed_consumer
        COMMAND "${CMAKE_COMMAND}" "-DCONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/installed_consumer.cmake")
    set_tests_properties(installed_consumer tests PROPERTIES TIMEOUT 180)
endif()
