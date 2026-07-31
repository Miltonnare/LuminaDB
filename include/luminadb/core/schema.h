#ifndef LUMINADB_CORE_SCHEMA_H
#define LUMINADB_CORE_SCHEMA_H

#include <string>
#include <vector>
#include "luminadb/common/types.h"
#include "luminadb/core/value.h"

namespace luminadb {

struct Column {
    std::string name;
    DataType type;
    bool is_primary_key{false};
    bool is_not_null{false};
    bool is_unique{false};
    bool has_default{false};
    Value default_val;
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::string name, std::vector<Column> cols);

    const std::string& name() const noexcept { return name_; }
    const std::vector<Column>& columns() const noexcept { return columns_; }

    int get_col_index(const std::string& col_name) const;
    const Column& get_col(int idx) const;

    std::vector<uint8_t> serialize() const;
    static Schema deserialize(const uint8_t* data, size_t size, size_t& bytes_read);

    std::vector<uint8_t> serialize_row(const std::vector<Value>& row, txn_id_t xmin = 0, txn_id_t xmax = 0) const;
    std::vector<Value> deserialize_row(const uint8_t* data, size_t size, txn_id_t* xmin = nullptr, txn_id_t* xmax = nullptr) const;

private:
    std::string name_;
    std::vector<Column> columns_;
};

} // namespace luminadb

#endif // LUMINADB_CORE_SCHEMA_H
