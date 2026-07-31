#ifndef LUMINADB_COMMON_TYPES_H
#define LUMINADB_COMMON_TYPES_H

#include <cstdint>
#include <cstddef>
#include <string_view>


namespace luminadb {

using page_id_t = int32_t;
using txn_id_t = int32_t;
using frame_id_t = int32_t;
using lsn_t = int64_t;
using slot_offset_t = uint16_t;
using record_id_t = uint64_t; // High 32 bits page_id, Low 32 bits slot_id

static constexpr page_id_t INVALID_PAGE_ID = -1;
static constexpr txn_id_t INVALID_TXN_ID = -1;
static constexpr lsn_t INVALID_LSN = -1;
static constexpr size_t PAGE_SIZE = 4096;

inline record_id_t make_record_id(page_id_t page_id, uint32_t slot_id) {
    return (static_cast<record_id_t>(page_id) << 32) | slot_id;
}

inline page_id_t get_page_id(record_id_t rid) {
    return static_cast<page_id_t>(rid >> 32);
}

inline uint32_t get_slot_id(record_id_t rid) {
    return static_cast<uint32_t>(rid & 0xFFFFFFFF);
}

enum class DataType : uint8_t {
    NONE = 0,
    INT = 1,
    FLOAT = 2,
    VARCHAR = 3,
    BLOB = 4
};

inline std::string_view data_type_to_string(DataType type) {
    switch (type) {
        case DataType::NONE: return "NONE";
        case DataType::INT: return "INT";
        case DataType::FLOAT: return "FLOAT";
        case DataType::VARCHAR: return "VARCHAR";
        case DataType::BLOB: return "BLOB";
    }
    return "UNKNOWN";
}

} // namespace luminadb

#endif // LUMINADB_COMMON_TYPES_H
