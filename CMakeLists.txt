# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.21)
project(zcode_cpp VERSION 0.1.0 LANGUAGES CXX)

option(ZCODE_BUILD_TESTS "Build the GoogleTest suite" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(ZCODE_WARNINGS /W4 /permissive- /utf-8)
else()
    set(ZCODE_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# ---- data/*.tsv embedded at build time --------------------------------------
set(ZCODE_DATA_TABLES zcode shapes rules keyboard dictionary)
set(ZCODE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ZCODE_GENERATED_HEADERS)
foreach(table IN LISTS ZCODE_DATA_TABLES)
    set(in ${CMAKE_CURRENT_SOURCE_DIR}/data/${table}.tsv)
    set(out ${ZCODE_GENERATED_DIR}/zcode_data_${table}.h)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${in} -DOUTPUT=${out} -DSYMBOL=zcode_data_${table}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${in} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        COMMENT "Embedding data/${table}.tsv"
        VERBATIM)
    list(APPEND ZCODE_GENERATED_HEADERS ${out})
endforeach()

# ---- library ------------------------------------------------------------------
add_library(zcode
    src/decomposer.cpp
    src/dict/dictionary.cpp
    src/dict/ime.cpp
    src/dict/scan_index.cpp
    src/dict/user_words.cpp
    src/dict/trie.cpp
    src/dict/word_data_embedded.cpp
    src/engine.cpp
    src/key_filter.cpp
    src/keyboard_layout.cpp
    src/letter_shape_sequence.cpp
    src/letter_splicer.cpp
    src/rules.cpp
    src/session.cpp
    src/splice_table.cpp
    src/tables.cpp
    src/tables_embedded.cpp
    src/verifier.cpp
    ${ZCODE_GENERATED_HEADERS})
add_library(zcode::zcode ALIAS zcode)
target_include_directories(zcode
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZCODE_GENERATED_DIR})
target_compile_options(zcode PRIVATE ${ZCODE_WARNINGS})

# ---- tests --------------------------------------------------------------------
if(ZCODE_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/releases/download/v1.16.0/googletest-1.16.0.tar.gz
            URL_HASH SHA256=78c676fc63881529bf97bf9d45948d905a66833fbfa5318ea2cd7478cb98f399
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(zcode_tests
        tests/test_dictionary.cpp
        tests/test_golden.cpp
        tests/test_options.cpp
        tests/test_protocol.cpp
        service/protocol.cpp
        tests/test_rules.cpp
        tests/test_session.cpp
        tests/test_splice_table.cpp
        tests/test_tables.cpp
        tests/test_user_words.cpp)
    target_link_libraries(zcode_tests PRIVATE zcode GTest::gtest_main)
    # white-box tests compare internal tables with the Java dumps
    target_include_directories(zcode_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(zcode_tests PRIVATE ZCODE_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
    target_compile_options(zcode_tests PRIVATE ${ZCODE_WARNINGS})
    include(GoogleTest)
    gtest_discover_tests(zcode_tests)
endif()

# ---- stdio service ------------------------------------------------------------
# The engine as a separate process, so a host can use it without linking this
# GPL-2.0-or-later code in. Protocol in service/protocol.h.
option(ZCODE_BUILD_SERVICE "Build the stdio service" ON)
if(ZCODE_BUILD_SERVICE)
    add_executable(zcode_stdio service/zcode_stdio.cpp service/protocol.cpp)
    target_link_libraries(zcode_stdio PRIVATE zcode)
    target_compile_options(zcode_stdio PRIVATE ${ZCODE_WARNINGS})
endif()

# ---- benchmark ----------------------------------------------------------------
option(ZCODE_BUILD_BENCH "Build the benchmark tool" ON)
if(ZCODE_BUILD_BENCH)
    add_executable(zcode_bench bench/zcode_bench.cpp)
    target_link_libraries(zcode_bench PRIVATE zcode)
    target_include_directories(zcode_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(zcode_bench PRIVATE ZCODE_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
    target_compile_options(zcode_bench PRIVATE ${ZCODE_WARNINGS})

    # Splits one keystroke's cost into splicing, searching and scoring.
    add_executable(zcode_dict_profile bench/dict_profile.cpp)
    target_link_libraries(zcode_dict_profile PRIVATE zcode)
    target_compile_options(zcode_dict_profile PRIVATE ${ZCODE_WARNINGS})
endif()
