#ifndef LUMINADB_COMMON_ERRORS_H
#define LUMINADB_COMMON_ERRORS_H

#include <string>
#include <exception>
#include <string_view>

namespace luminadb {

enum class StatusCode {
    OK = 0,
    ERR_CORRUPT_INPUT,
    ERR_OUT_OF_BOUNDS,
    ERR_KEY_NOT_FOUND,
    ERR_DUPLICATE_KEY,
    ERR_PARSE_ERROR,
    ERR_TYPE_MISMATCH,
    ERR_IO_ERROR,
    ERR_TRANSACTION_ABORTED,
    ERR_SCHEMA_VIOLATION,
    ERR_INTERNAL
};

std::string_view status_code_to_string(StatusCode code);

class DBException : public std::exception {
public:
    DBException(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    StatusCode code() const noexcept {
        return code_;
    }

private:
    StatusCode code_;
    std::string message_;
};

} // namespace luminadb

#endif // LUMINADB_COMMON_ERRORS_H
