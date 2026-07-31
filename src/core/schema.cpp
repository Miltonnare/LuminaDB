#include "luminadb/core/schema.h"
#include "luminadb/core/tlv.h"

namespace luminadb {

Schema::Schema(std::string name, std::vector<Column> cols)
    : name_(std::move(name)), columns_(std::move(cols)) {
    if (name_.empty()) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table name cannot be empty");
    }
    if (columns_.empty()) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table must have at least one column");
    }
}

int Schema::get_col_index(const std::string& col_name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == col_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const Column& Schema::get_col(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(columns_.size())) {
        throw DBException(StatusCode::ERR_OUT_OF_BOUNDS, "Column index out of bounds");
    }
    return columns_[idx];
}

std::vector<uint8_t> Schema::serialize() const {
    std::vector<uint8_t> buf;

    // Table name length (2 bytes) + name
    uint16_t name_len = static_cast<uint16_t>(name_.size());
    buf.push_back(static_cast<uint8_t>((name_len >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(name_len & 0xFF));
    for (char c : name_) {
        buf.push_back(static_cast<uint8_t>(c));
    }

    // Number of columns (2 bytes)
    uint16_t num_cols = static_cast<uint16_t>(columns_.size());
    buf.push_back(static_cast<uint8_t>((num_cols >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(num_cols & 0xFF));

    // For each column:
    for (const auto& col : columns_) {
        // Name length (2 bytes) + name
        uint16_t col_name_len = static_cast<uint16_t>(col.name.size());
        buf.push_back(static_cast<uint8_t>((col_name_len >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(col_name_len & 0xFF));
        for (char c : col.name) {
            buf.push_back(static_cast<uint8_t>(c));
        }

        // DataType (1 byte)
        buf.push_back(static_cast<uint8_t>(col.type));

        // Is primary key (1 byte)
        buf.push_back(static_cast<uint8_t>(col.is_primary_key ? 1 : 0));

        // Is not null (1 byte)
        buf.push_back(static_cast<uint8_t>(col.is_not_null ? 1 : 0));

        // Is unique (1 byte)
        buf.push_back(static_cast<uint8_t>(col.is_unique ? 1 : 0));

        // Has default (1 byte)
        buf.push_back(static_cast<uint8_t>(col.has_default ? 1 : 0));

        if (col.has_default) {
            TLVCoder::encode_to(col.default_val, buf);
        }
    }

    return buf;
}

Schema Schema::deserialize(const uint8_t* data, size_t size, size_t& bytes_read) {
    if (size < 4) {
        throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Input too short for schema header");
    }

    size_t offset = 0;
    uint16_t name_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;

    if (offset + name_len + 2 > size) {
        throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Schema table name truncated");
    }

    std::string name(reinterpret_cast<const char*>(data + offset), name_len);
    offset += name_len;

    uint16_t num_cols = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;

    std::vector<Column> cols;
    for (uint16_t i = 0; i < num_cols; ++i) {
        if (offset + 2 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Schema column header truncated");
        }
        uint16_t col_name_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;

        if (offset + col_name_len + 5 > size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Schema column name or flags truncated");
        }

        std::string col_name(reinterpret_cast<const char*>(data + offset), col_name_len);
        offset += col_name_len;

        uint8_t type_byte = data[offset++];
        if (type_byte > static_cast<uint8_t>(DataType::BLOB)) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Invalid data type in schema: " + std::to_string(type_byte));
        }
        DataType type = static_cast<DataType>(type_byte);

        bool is_pk = data[offset++] != 0;
        bool is_nn = data[offset++] != 0;
        bool is_uq = data[offset++] != 0;
        bool has_df = data[offset++] != 0;

        Value df_val;
        if (has_df) {
            size_t df_read = 0;
            df_val = TLVCoder::decode(data + offset, size - offset, df_read);
            offset += df_read;
        }

        cols.push_back(Column{std::move(col_name), type, is_pk, is_nn, is_uq, has_df, std::move(df_val)});
    }

    bytes_read = offset;
    return Schema(std::move(name), std::move(cols));
}

std::vector<uint8_t> Schema::serialize_row(const std::vector<Value>& row, txn_id_t xmin, txn_id_t xmax) const {
    if (row.size() != columns_.size()) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Row size does not match schema column count");
    }

    std::vector<uint8_t> buf(16);
    std::memcpy(buf.data(), &xmin, 8);
    std::memcpy(buf.data() + 8, &xmax, 8);

    for (size_t i = 0; i < columns_.size(); ++i) {
        // Validate type
        if (!row[i].is_null() && row[i].type() != columns_[i].type) {
            throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Row value type does not match schema for column: " + columns_[i].name);
        }
        // Validate NOT NULL constraint
        if (row[i].is_null() && columns_[i].is_not_null) {
            throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "NOT NULL constraint violated for column: " + columns_[i].name);
        }
        TLVCoder::encode_to(row[i], buf);
    }
    return buf;
}

std::vector<Value> Schema::deserialize_row(const uint8_t* data, size_t size, txn_id_t* xmin, txn_id_t* xmax) const {
    txn_id_t local_xmin = 0;
    txn_id_t local_xmax = 0;
    size_t offset = 0;

    if (size >= 16) {
        std::memcpy(&local_xmin, data, 8);
        std::memcpy(&local_xmax, data + 8, 8);
        offset = 16;
    }

    if (xmin) *xmin = local_xmin;
    if (xmax) *xmax = local_xmax;

    std::vector<Value> row;
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (offset >= size) {
            throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Row data truncated, missing column: " + columns_[i].name);
        }
        size_t bytes_read = 0;
        Value val = TLVCoder::decode(data + offset, size - offset, bytes_read);
        if (!val.is_null() && val.type() != columns_[i].type) {
            throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Deserialized row value type mismatch for column: " + columns_[i].name);
        }
        row.push_back(std::move(val));
        offset += bytes_read;
    }
    return row;
}

} // namespace luminadb
