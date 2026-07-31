# LuminaDB

LuminaDB is a transactional relational-style database engine written in C++17. It manages records using slotted-page files, provides primary-key index lookups via B+Trees, maintains recovery logs through a Write-Ahead Log (WAL) system, and offers a SQL-like parser and executor.

## Architectural Overview

LuminaDB consists of several decoupled layers:
1. **TLV Core**: Handles Type-Length-Value serialization for various data types (Integers, Floats, Strings, and BLOBs) with rigid bounds and size validation.
2. **Storage Management**: Page files are structured as virtual page slots. The `DiskManager` manages reads/writes to OS filesystem blocks, and `TableHeap` handles record mapping.
3. **Transaction Recovery (WAL)**: An ARIES-based logging manager captures database state changes before applying mutations, executing Redo on committed transactions and Undo on aborted/uncommitted transactions upon database boot or txn failures.
4. **B+Tree Indexing**: Primary key records are mapped using an in-memory B+Tree that automatically splits internal and leaf nodes, enabling fast point queries and range-based index scans.
5. **SQL Parser & Query Executor**: Lexes inputs into standard tokens and constructs an Abstract Syntax Tree (AST). The Query Executor runs queries, applying index-scan optimizations when primary key clauses are detected.

---

## Directory Structure

```
├── .clusterfuzzlite/       # ClusterFuzzLite build script integration
├── .gitignore             # Standard VCS exclusions
├── CMakeLists.txt         # Build configurations
├── fuzz/                  # LibFuzzer targets for parser, page storage, and TLV
├── include/
│   └── luminadb/          # Public C++ headers
└── src/                   # Implementation files
```

---

## Build Instructions

LuminaDB uses CMake for standard offline compilation.

### Prerequisites

* C++17 compliant compiler (GCC 8+, Clang 7+, or MSVC 2019+)
* CMake (Version 3.12+)

### Standard Build

To build the static library and unit tests:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To run unit tests:

```bash
./build/luminadb_tests
```

### Compiler Sanitizers Build

To build with AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```bash
cmake -B build -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

---

## Fuzzing Integration

LuminaDB provides multiple targets for libFuzzer and ClusterFuzzLite integrations.

To build fuzzers locally:

```bash
cmake -B build -DBUILD_FUZZERS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The compiled binaries (`query_fuzzer`, `storage_fuzzer`, `tlv_fuzzer`) will be located in the build directory and can be executed directly.

---

## Integration & Static Analysis

### 1. Header-Only Integration Rules
To integrate external header-only libraries (e.g. JSON or XML parsers) while keeping the database hermetic and sanitizer-compatible:
* **Directory Isolation**: Place all external headers in a dedicated nested folder (e.g., `include/luminadb/thirdparty/`). Never mix thirdparty code with project headers.
* **Sanitizer Compatibility**: Compile thirdparty templates with matching sanitizer flags (`-fsanitize=address,undefined`) to avoid Link-time/ODR violations.
* **No Macro Pollution**: Wrap inclusions with appropriate guard macro undefs if the thirdparty headers pollute standard namespace macros.

### 2. Static Analysis & Clang-Tidy
LuminaDB is configured to output a compilation database dynamically:
* **Generate compile_commands.json**: Setting `-DEXPORT_COMPILE_COMMANDS=ON` (which is configured by default via `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` in standard Makefile or Ninja generators) outputs `compile_commands.json` in the build folder during configuration.
* **Run Clang-Tidy**: To perform static analysis verification during the validation stage:
  ```bash
  clang-tidy -p build/ src/**/*.cpp
  ```

