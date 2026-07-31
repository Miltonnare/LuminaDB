#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include "luminadb/query/parser.h"
#include "luminadb/query/lexer.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    std::string sql(reinterpret_cast<const char*>(data), size);

    try {
        luminadb::Parser parser(sql);
        auto stmt = parser.parse();
    } catch (...) {
        // We expect parse failures for arbitrary strings; we want to make sure 
        // they are thrown gracefully and do not cause crashes/leaks.
    }

    return 0;
}
