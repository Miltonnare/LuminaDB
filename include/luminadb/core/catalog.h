#ifndef LUMINADB_CORE_CATALOG_H
#define LUMINADB_CORE_CATALOG_H

#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include "luminadb/core/schema.h"
#include "luminadb/storage/table_heap.h"
#include "luminadb/storage/btree_index.h"

namespace luminadb {

struct ColumnStats {
    int64_t num_distinct{0};
    Value min_val;
    Value max_val;
    int64_t null_count{0};
};

struct TableStats {
    int64_t num_rows{0};
    int64_t avg_row_size{0};
    std::unordered_map<std::string, ColumnStats> col_stats;
};

struct TableMetadata {
    std::string name;
    Schema schema;
    std::unique_ptr<TableHeap> table_heap;
    record_id_t root_rid{0}; // For serialization tracking
    TableStats stats;
};

struct IndexMetadata {
    std::string name;
    std::string table_name;
    std::vector<int> key_attrs;
    std::unique_ptr<BTreeIndex> index;
};

class Catalog {
public:
    Catalog() = default;
    ~Catalog() = default;

    // Prevent copy/move
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    // Table actions
    bool create_table(std::string name, Schema schema, DiskManager& disk);
    TableMetadata* get_table(const std::string& name) const;
    std::vector<std::string> get_table_names() const;

    // Index actions
    bool create_index(std::string index_name, std::string table_name, std::vector<int> key_attrs);
    IndexMetadata* get_index(const std::string& index_name) const;
    std::vector<IndexMetadata*> get_table_indexes(const std::string& table_name) const;

    // Catalog persistence
    std::vector<uint8_t> serialize() const;
    void deserialize(const uint8_t* data, size_t size, DiskManager& disk);

private:
    mutable std::mutex latch_;
    std::unordered_map<std::string, std::unique_ptr<TableMetadata>> tables_;
    std::unordered_map<std::string, std::unique_ptr<IndexMetadata>> indexes_;
    std::unordered_map<std::string, std::vector<std::string>> table_indexes_map_;
};

} // namespace luminadb

#endif // LUMINADB_CORE_CATALOG_H
