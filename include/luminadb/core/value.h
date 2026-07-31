#ifndef LUMINADB_CORE_VALUE_H
#define LUMINADB_CORE_VALUE_H

#include <string>
#include <vector>
#include <variant>
#include "luminadb/common/types.h"
#include "luminadb/common/errors.h"

namespace luminadb {

class Value {
public:
    Value();
    explicit Value(int64_t v);
    explicit Value(double v);
    explicit Value(std::string v);
    explicit Value(std::vector<uint8_t> v);

    DataType type() const noexcept;
    bool is_null() const noexcept;

    int64_t as_int() const;
    double as_float() const;
    const std::string& as_string() const;
    const std::vector<uint8_t>& as_blob() const;

    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const;
    bool operator<(const Value& o) const;
    bool operator<=(const Value& o) const;
    bool operator>(const Value& o) const;
    bool operator>=(const Value& o) const;

    std::string to_string() const;

private:
    DataType type_;
    std::variant<std::monostate, int64_t, double, std::string, std::vector<uint8_t>> val_;
};

} // namespace luminadb

#endif // LUMINADB_CORE_VALUE_H
