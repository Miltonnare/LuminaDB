cmake_minimum_required(VERSION 3.12)
project(LuminaDB CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
include_directories(include)

# Source files list
set(LUMINADB_SOURCES
    src/common/errors.cpp
    src/core/value.cpp
    src/core/tlv.cpp
    src/core/schema.cpp
    src/core/catalog.cpp
    src/storage/disk_manager.cpp
    src/storage/replacer.cpp
    src/storage/buffer_pool_manager.cpp
    src/storage/wal.cpp
    src/storage/btree_index.cpp
    src/storage/table_heap.cpp
    src/concurrency/lock_manager.cpp
    src/concurrency/transaction_manager.cpp
    src/query/lexer.cpp
    src/query/parser.cpp
    src/query/planner.cpp
    src/query/optimizer.cpp
    src/query/executor.cpp
    src/db.cpp
)

# Core library
add_library(luminadb STATIC ${LUMINADB_SOURCES})

# Enable sanitizers option
option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior sanitizers" OFF)
if (ENABLE_SANITIZERS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(luminadb PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(luminadb PUBLIC -fsanitize=address,undefined)
    elseif (MSVC)
        # MSVC supports AddressSanitizer starting from VS 2019
        target_compile_options(luminadb PUBLIC /fsanitize=address)
    endif()
endif()

# Build tests by default
option(BUILD_TESTS "Build unit tests" ON)
if (BUILD_TESTS)
    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_tlv.cpp
        tests/test_parser.cpp
        tests/test_storage.cpp
        tests/test_query.cpp
    )
    add_executable(luminadb_tests ${TEST_SOURCES})
    target_link_libraries(luminadb_tests PRIVATE luminadb)
    
    if (ENABLE_SANITIZERS)
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(luminadb_tests PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(luminadb_tests PUBLIC -fsanitize=address,undefined)
        elseif (MSVC)
            target_compile_options(luminadb_tests PUBLIC /fsanitize=address)
        endif()
    endif()
endif()

# Build CLI executable
add_executable(luminadb_cli src/main.cpp)
target_link_libraries(luminadb_cli PRIVATE luminadb)

if (ENABLE_SANITIZERS)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(luminadb_cli PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(luminadb_cli PUBLIC -fsanitize=address,undefined)
    elseif (MSVC)
        target_compile_options(luminadb_cli PUBLIC /fsanitize=address)
    endif()
endif()


# Build fuzzers option (usually enabled for ClusterFuzzLite)
option(BUILD_FUZZERS "Build fuzzers with fuzzing engine" OFF)
if (BUILD_FUZZERS)
    # Check for external libfuzzer engine variable or use default
    set(FUZZING_ENGINE_FLAG "$ENV{LIB_FUZZING_ENGINE}")
    if (NOT FUZZING_ENGINE_FLAG)
        set(FUZZING_ENGINE_FLAG "-fsanitize=fuzzer")
    endif()

    message(STATUS "Building fuzzers with fuzzing engine: ${FUZZING_ENGINE_FLAG}")

    # Helper function to add a fuzz target
    function(add_fuzz_target TARGET_NAME SOURCE_FILE)
        add_executable(${TARGET_NAME} ${SOURCE_FILE})
        target_link_libraries(${TARGET_NAME} PRIVATE luminadb)
        
        # Apply fuzzing engine flags
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${TARGET_NAME} PRIVATE ${FUZZING_ENGINE_FLAG} -fsanitize=address,undefined -fno-omit-frame-pointer)
            # Make sure to split the flags if it's multiple (e.g. if it is a file path)
            target_link_options(${TARGET_NAME} PRIVATE ${FUZZING_ENGINE_FLAG} -fsanitize=address,undefined)
        endif()
    endfunction()

    add_fuzz_target(query_fuzzer fuzz/query_fuzzer.cpp)
    add_fuzz_target(storage_fuzzer fuzz/storage_fuzzer.cpp)
    add_fuzz_target(tlv_fuzzer fuzz/tlv_fuzzer.cpp)
endif()
