#include <cstdint>
#include <cstddef>
#include "luminadb/core/tlv.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    size_t bytes_read = 0;
    try {
        luminadb::TLVCoder::decode(data, size, bytes_read);
    } catch (...) {
        // Safe to ignore parser/deserialization exceptions
    }
    return 0;
}
