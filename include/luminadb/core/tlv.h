#ifndef LUMINADB_CORE_TLV_H
#define LUMINADB_CORE_TLV_H

#include <vector>
#include <cstdint>
#include "luminadb/core/value.h"

namespace luminadb {

class TLVCoder {
public:
    static std::vector<uint8_t> encode(const Value& val);
    static void encode_to(const Value& val, std::vector<uint8_t>& buf);
    
    // Decodes a single value from data. On success, updates bytes_read.
    // Throws DBException if invalid/truncated.
    static Value decode(const uint8_t* data, size_t size, size_t& bytes_read);
};

} // namespace luminadb

#endif // LUMINADB_CORE_TLV_H
