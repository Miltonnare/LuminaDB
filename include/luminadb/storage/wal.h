#ifndef LUMINADB_STORAGE_WAL_H
#define LUMINADB_STORAGE_WAL_H

#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include "luminadb/common/types.h"
#include "luminadb/common/errors.h"

namespace luminadb {

enum class LogRecordType : uint32_t {
    BEGIN = 0,
    COMMIT = 1,
    ABORT = 2,
    UPDATE = 3,
    SAVEPOINT = 4,
    RELEASE_SAVEPOINT = 5,
    ROLLBACK_TO = 6
};

struct LogRecord {
    lsn_t lsn{INVALID_LSN};
    txn_id_t txn_id{INVALID_TXN_ID};
    LogRecordType type{LogRecordType::BEGIN};

    // For UPDATE records
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_id{0};
    std::vector<uint8_t> before_image;
    std::vector<uint8_t> after_image;
};

class WALManager {
public:
    explicit WALManager(std::string log_file);
    ~WALManager();

    // Append a log record to the in-memory buffer / file
    lsn_t append_record(txn_id_t txn_id, LogRecordType type, 
                        page_id_t page_id = INVALID_PAGE_ID, 
                        uint32_t slot_id = 0, 
                        std::vector<uint8_t> before_img = {}, 
                        std::vector<uint8_t> after_img = {});

    // Force flush the log file to disk
    void flush();

    // Read all records sequentially from the log file (for recovery)
    std::vector<LogRecord> read_all_records();

    void close();

private:
    std::string log_file_name_;
    std::fstream log_io_;
    lsn_t next_lsn_{0};
    std::mutex log_lck_;
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_WAL_H
