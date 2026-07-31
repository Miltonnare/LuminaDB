#include "luminadb/core/value.h"
#include <sstream>
#include <iomanip>

namespace luminadb {

Value::Value() : type_(DataType::NONE), val_(std::monostate{}) {}
Value::Value(int64_t v) : type_(DataType::INT), val_(v) {}
Value::Value(double v) : type_(DataType::FLOAT), val_(v) {}
Value::Value(std::string v) : type_(DataType::VARCHAR), val_(std::move(v)) {}
Value::Value(std::vector<uint8_t> v) : type_(DataType::BLOB), val_(std::move(v)) {}

DataType Value::type() const noexcept {
    return type_;
}

bool Value::is_null() const noexcept {
    return type_ == DataType::NONE;
}

int64_t Value::as_int() const {
    if (type_ != DataType::INT) {
        throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value is not an integer");
    }
    return std::get<int64_t>(val_);
}

double Value::as_float() const {
    if (type_ != DataType::FLOAT) {
        throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value is not a float");
    }
    return std::get<double>(val_);
}

const std::string& Value::as_string() const {
    if (type_ != DataType::VARCHAR) {
        throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value is not a string");
    }
    return std::get<std::string>(val_);
}

const std::vector<uint8_t>& Value::as_blob() const {
    if (type_ != DataType::BLOB) {
        throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value is not a blob");
    }
    return std::get<std::vector<uint8_t>>(val_);
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) {
        // Safe comparison between INT and FLOAT
        if (type_ == DataType::INT && o.type_ == DataType::FLOAT) {
            return static_cast<double>(std::get<int64_t>(val_)) == std::get<double>(o.val_);
        }
        if (type_ == DataType::FLOAT && o.type_ == DataType::INT) {
            return std::get<double>(val_) == static_cast<double>(std::get<int64_t>(o.val_));
        }
        return false;
    }
    return val_ == o.val_;
}

bool Value::operator!=(const Value& o) const {
    return !(*this == o);
}

bool Value::operator<(const Value& o) const {
    if (type_ != o.type_) {
        if (type_ == DataType::INT && o.type_ == DataType::FLOAT) {
            return static_cast<double>(std::get<int64_t>(val_)) < std::get<double>(o.val_);
        }
        if (type_ == DataType::FLOAT && o.type_ == DataType::INT) {
            return std::get<double>(val_) < static_cast<double>(std::get<int64_t>(o.val_));
        }
        throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Cannot compare values of different types");
    }
    if (type_ == DataType::NONE) return false;
    return val_ < o.val_;
}

bool Value::operator<=(const Value& o) const {
    return (*this < o) || (*this == o);
}

bool Value::operator>(const Value& o) const {
    return !(*this <= o);
}

bool Value::operator>=(const Value& o) const {
    return !(*this < o);
}

std::string Value::to_string() const {
    switch (type_) {
        case DataType::NONE:
            return "NULL";
        case DataType::INT:
            return std::to_string(std::get<int64_t>(val_));
        case DataType::FLOAT: {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(6) << std::get<double>(val_);
            std::string s = ss.str();
            // remove trailing zeros
            s.erase(s.find_last_not_of('0') + 1, std::string::npos);
            if (s.back() == '.') {
                s.pop_back();
            }
            return s;
        }
        case DataType::VARCHAR:
            return std::get<std::string>(val_);
        case DataType::BLOB: {
            const auto& b = std::get<std::vector<uint8_t>>(val_);
            std::ostringstream ss;
            ss << "BLOB(" << b.size() << " bytes)";
            return ss.str();
        }
    }
    return "UNKNOWN";
}

} // namespace luminadb
