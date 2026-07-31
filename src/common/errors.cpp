#include "luminadb/common/errors.h"

namespace luminadb {

std::string_view status_code_to_string(StatusCode code) {
    switch (code) {
        case StatusCode::OK:
            return "OK";
        case StatusCode::ERR_CORRUPT_INPUT:
            return "Corrupt Input";
        case StatusCode::ERR_OUT_OF_BOUNDS:
            return "Out Of Bounds";
        case StatusCode::ERR_KEY_NOT_FOUND:
            return "Key Not Found";
        case StatusCode::ERR_DUPLICATE_KEY:
            return "Duplicate Key";
        case StatusCode::ERR_PARSE_ERROR:
            return "Parse Error";
        case StatusCode::ERR_TYPE_MISMATCH:
            return "Type Mismatch";
        case StatusCode::ERR_IO_ERROR:
            return "I/O Error";
        case StatusCode::ERR_TRANSACTION_ABORTED:
            return "Transaction Aborted";
        case StatusCode::ERR_SCHEMA_VIOLATION:
            return "Schema Violation";
        case StatusCode::ERR_INTERNAL:
            return "Internal Error";
        default:
            return "Unknown Error";
    }
}

} // namespace luminadb
