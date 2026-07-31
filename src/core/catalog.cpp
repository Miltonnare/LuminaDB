#include "luminadb/core/catalog.h"
#include <cstring>

namespace luminadb {

bool Catalog::create_table(std::string name, Schema schema, DiskManager& disk) {
    std::lock_guard<std::mutex> lock(latch_);
    if (tables_.find(name) != tables_.end()) {
        return false; // Table already exists
    }

    auto meta = std::make_unique<TableMetadata>();
    meta->name = name;
    meta->schema = std::move(schema);
    meta->table_heap = std::make_unique<TableHeap>(disk);

    tables_[name] = std::move(meta);
    return true;
}

TableMetadata* Catalog::get_table(const std::string& name) const {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = tables_.find(name);
    if (iter == tables_.end()) {
        return nullptr;
    }
    return iter->second.get();
}

std::vector<std::string> Catalog::get_table_names() const {
    std::lock_guard<std::mutex> lock(latch_);
    std::vector<std::string> names;
    for (const auto& pair : tables_) {
        names.push_back(pair.first);
    }
    return names;
}

bool Catalog::create_index(std::string index_name, std::string table_name, std::vector<int> key_attrs) {
    std::lock_guard<std::mutex> lock(latch_);
    if (indexes_.find(index_name) != indexes_.end()) {
        return false;
    }
    if (tables_.find(table_name) == tables_.end()) {
        return false; // Table does not exist
    }

    auto meta = std::make_unique<IndexMetadata>();
    meta->name = index_name;
    meta->table_name = table_name;
    meta->key_attrs = std::move(key_attrs);
    meta->index = std::make_unique<BTreeIndex>();

    indexes_[index_name] = std::move(meta);
    table_indexes_map_[table_name].push_back(index_name);
    return true;
}

IndexMetadata* Catalog::get_index(const std::string& index_name) const {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = indexes_.find(index_name);
    if (iter == indexes_.end()) {
        return nullptr;
    }
    return iter->second.get();
}

std::vector<IndexMetadata*> Catalog::get_table_indexes(const std::string& table_name) const {
    std::lock_guard<std::mutex> lock(latch_);
    std::vector<IndexMetadata*> list;
    auto iter = table_indexes_map_.find(table_name);
    if (iter != table_indexes_map_.end()) {
        for (const auto& idx_name : iter->second) {
            list.push_back(indexes_.at(idx_name).get());
        }
    }
    return list;
}

std::vector<uint8_t> Catalog::serialize() const {
    std::lock_guard<std::mutex> lock(latch_);
    std::vector<uint8_t> buf;

    // Number of tables
    uint16_t num_tables = static_cast<uint16_t>(tables_.size());
    buf.push_back(static_cast<uint8_t>((num_tables >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(num_tables & 0xFF));

    // Serialize each table schema
    for (const auto& pair : tables_) {
        const auto& meta = pair.second;
        std::vector<uint8_t> schema_serialized = meta->schema.serialize();
        page_id_t first_pid = meta->table_heap->get_first_page_id();

        // Write first_page_id (4 bytes)
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((first_pid >> (i * 8)) & 0xFF));
        }

        // Write num_rows (8 bytes)
        int64_t num_rows = meta->stats.num_rows;
        for (int i = 7; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((num_rows >> (i * 8)) & 0xFF));
        }

        // Write avg_row_size (8 bytes)
        int64_t avg_row_size = meta->stats.avg_row_size;
        for (int i = 7; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((avg_row_size >> (i * 8)) & 0xFF));
        }

        // Write size of table schema block (4 bytes)
        uint32_t b_len = static_cast<uint32_t>(schema_serialized.size());
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((b_len >> (i * 8)) & 0xFF));
        }
        buf.insert(buf.end(), schema_serialized.begin(), schema_serialized.end());
    }

    // Number of indexes
    uint16_t num_indexes = static_cast<uint16_t>(indexes_.size());
    buf.push_back(static_cast<uint8_t>((num_indexes >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(num_indexes & 0xFF));

    // Serialize each index
    for (const auto& pair : indexes_) {
        const auto& meta = pair.second;
        
        // Index name len + string
        uint16_t name_len = static_cast<uint16_t>(meta->name.size());
        buf.push_back(static_cast<uint8_t>((name_len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(name_len & 0xFF));
        buf.insert(buf.end(), meta->name.begin(), meta->name.end());

        // Table name len + string
        uint16_t t_len = static_cast<uint16_t>(meta->table_name.size());
        buf.push_back(static_cast<uint8_t>((t_len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(t_len & 0xFF));
        buf.insert(buf.end(), meta->table_name.begin(), meta->table_name.end());

        // Key attributes size
        uint16_t num_attrs = static_cast<uint16_t>(meta->key_attrs.size());
        buf.push_back(static_cast<uint8_t>((num_attrs >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(num_attrs & 0xFF));
        for (int attr : meta->key_attrs) {
            buf.push_back(static_cast<uint8_t>((attr >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(attr & 0xFF));
        }
    }

    return buf;
}

void Catalog::deserialize(const uint8_t* data, size_t size, DiskManager& disk) {
    std::lock_guard<std::mutex> lock(latch_);
    tables_.clear();
    indexes_.clear();
    table_indexes_map_.clear();

    if (size < 2) return;

    size_t offset = 0;
    uint16_t num_tables = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;

    for (uint16_t i = 0; i < num_tables; ++i) {
        if (offset + 24 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization failed: truncated table metadata");
        }
        page_id_t first_pid = (static_cast<page_id_t>(data[offset]) << 24) |
                              (static_cast<page_id_t>(data[offset+1]) << 16) |
                              (static_cast<page_id_t>(data[offset+2]) << 8)  |
                              (static_cast<page_id_t>(data[offset+3]));
        offset += 4;

        int64_t num_rows = 0;
        for (int j = 0; j < 8; ++j) {
            num_rows = (num_rows << 8) | data[offset + j];
        }
        offset += 8;

        int64_t avg_row_size = 0;
        for (int j = 0; j < 8; ++j) {
            avg_row_size = (avg_row_size << 8) | data[offset + j];
        }
        offset += 8;

        uint32_t b_len = (static_cast<uint32_t>(data[offset]) << 24) |
                         (static_cast<uint32_t>(data[offset+1]) << 16) |
                         (static_cast<uint32_t>(data[offset+2]) << 8)  |
                         (static_cast<uint32_t>(data[offset+3]));
        offset += 4;

        if (offset + b_len > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization failed: truncated schema data");
        }

        size_t bytes_read = 0;
        Schema s = Schema::deserialize(data + offset, b_len, bytes_read);
        offset += b_len;

        std::string table_name = s.name();
        auto meta = std::make_unique<TableMetadata>();
        meta->name = table_name;
        meta->schema = std::move(s);
        meta->table_heap = std::make_unique<TableHeap>(disk, first_pid);
        meta->stats.num_rows = num_rows;
        meta->stats.avg_row_size = avg_row_size;

        tables_[table_name] = std::move(meta);
    }

    if (offset + 2 > size) return;
    uint16_t num_indexes = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;

    for (uint16_t i = 0; i < num_indexes; ++i) {
        if (offset + 2 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization: truncated index name size");
        }
        uint16_t name_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;

        if (offset + name_len + 2 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization: truncated index name or table size");
        }
        std::string name(reinterpret_cast<const char*>(data + offset), name_len);
        offset += name_len;

        uint16_t t_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;

        if (offset + t_len + 2 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization: truncated table name data");
        }
        std::string table_name(reinterpret_cast<const char*>(data + offset), t_len);
        offset += t_len;

        uint16_t num_attrs = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;

        if (offset + num_attrs * 2 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Catalog deserialization: truncated key attributes list");
        }

        std::vector<int> key_attrs;
        for (uint16_t k = 0; k < num_attrs; ++k) {
            int attr = (static_cast<int>(data[offset]) << 8) | data[offset + 1];
            key_attrs.push_back(attr);
            offset += 2;
        }

        auto meta = std::make_unique<IndexMetadata>();
        meta->name = name;
        meta->table_name = table_name;
        meta->key_attrs = std::move(key_attrs);
        meta->index = std::make_unique<BTreeIndex>();

        indexes_[name] = std::move(meta);
        table_indexes_map_[table_name].push_back(name);
    }
}

} // namespace luminadb
