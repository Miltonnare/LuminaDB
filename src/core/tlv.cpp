#include "luminadb/core/tlv.h"
#include <cstring>

namespace luminadb {

std::vector<uint8_t> TLVCoder::encode(const Value& val) {
    std::vector<uint8_t> buf;
    encode_to(val, buf);
    return buf;
}

void TLVCoder::encode_to(const Value& val, std::vector<uint8_t>& buf) {
    DataType type = val.type();
    buf.push_back(static_cast<uint8_t>(type));

    switch (type) {
        case DataType::NONE: {
            // Null type has length 0
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(0);
            break;
        }
        case DataType::INT: {
            // Int64 takes 8 bytes
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(8);
            int64_t v = val.as_int();
            for (int i = 7; i >= 0; --i) {
                buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
            }
            break;
        }
        case DataType::FLOAT: {
            // Double takes 8 bytes
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(0);
            buf.push_back(8);
            double v = val.as_float();
            uint64_t uv;
            std::memcpy(&uv, &v, sizeof(double));
            for (int i = 7; i >= 0; --i) {
                buf.push_back(static_cast<uint8_t>((uv >> (i * 8)) & 0xFF));
            }
            break;
        }
        case DataType::VARCHAR: {
            const std::string& v = val.as_string();
            uint32_t len = static_cast<uint32_t>(v.size());
            buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
            for (char c : v) {
                buf.push_back(static_cast<uint8_t>(c));
            }
            break;
        }
        case DataType::BLOB: {
            const std::vector<uint8_t>& v = val.as_blob();
            uint32_t len = static_cast<uint32_t>(v.size());
            buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            buf.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(len & 0xFF));
            buf.insert(buf.end(), v.begin(), v.end());
            break;
        }
    }
}

Value TLVCoder::decode(const uint8_t* data, size_t size, size_t& bytes_read) {
    if (size < 5) {
        throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Input too short for TLV header");
    }

    uint8_t t_byte = data[0];
    if (t_byte > static_cast<uint8_t>(DataType::BLOB)) {
        throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Invalid data type in TLV header: " + std::to_string(t_byte));
    }
    DataType type = static_cast<DataType>(t_byte);

    uint32_t len = (static_cast<uint32_t>(data[1]) << 24) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[3]) << 8)  |
                   (static_cast<uint32_t>(data[4]));

    if (size < 5 + len) {
        throw DBException(StatusCode::ERR_CORRUPT_INPUT, "Input too short for TLV value: size=" + std::to_string(size) + " len=" + std::to_string(len));
    }

    bytes_read = 5 + len;

    switch (type) {
        case DataType::NONE: {
            if (len != 0) {
                throw DBException(StatusCode::ERR_CORRUPT_INPUT, "NULL type must have length 0");
            }
            return Value();
        }
        case DataType::INT: {
            if (len != 8) {
                throw DBException(StatusCode::ERR_CORRUPT_INPUT, "INT type must have length 8");
            }
            int64_t val = 0;
            for (int i = 0; i < 8; ++i) {
                val = (val << 8) | data[5 + i];
            }
            return Value(val);
        }
        case DataType::FLOAT: {
            if (len != 8) {
                throw DBException(StatusCode::ERR_CORRUPT_INPUT, "FLOAT type must have length 8");
            }
            uint64_t uv = 0;
            for (int i = 0; i < 8; ++i) {
                uv = (uv << 8) | data[5 + i];
            }
            double val;
            std::memcpy(&val, &uv, sizeof(double));
            return Value(val);
        }
        case DataType::VARCHAR: {
            std::string val(reinterpret_cast<const char*>(data + 5), len);
            return Value(val);
        }
        case DataType::BLOB: {
            std::vector<uint8_t> val(data + 5, data + 5 + len);
            return Value(val);
        }
    }
    throw DBException(StatusCode::ERR_INTERNAL, "Unreached state in TLVCoder::decode");
}

} // namespace luminadb
