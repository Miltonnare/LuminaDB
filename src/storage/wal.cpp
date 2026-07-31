#include "luminadb/storage/wal.h"
#include <cstring>
#include <iostream>

namespace luminadb {

WALManager::WALManager(std::string log_file) : log_file_name_(std::move(log_file)) {
    log_io_.open(log_file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!log_io_.is_open()) {
        log_io_.clear();
        log_io_.open(log_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!log_io_.is_open()) {
            throw DBException(StatusCode::ERR_IO_ERROR, "Failed to open or create log file: " + log_file_name_);
        }
    }

    // Determine the next LSN based on existing file content size
    log_io_.seekp(0, std::ios::end);
    std::streamoff size = log_io_.tellp();
    if (size < 0) {
        throw DBException(StatusCode::ERR_IO_ERROR, "Failed to inspect log file size");
    }

    if (size == 0) {
        next_lsn_ = 0;
    } else {
        // We'll read all records to determine the latest LSN, or read the last LSN.
        // Reading all records is safest for recovery.
        try {
            auto records = read_all_records();
            if (!records.empty()) {
                next_lsn_ = records.back().lsn + 1;
            } else {
                next_lsn_ = 0;
            }
        } catch (...) {
            // If the log is corrupt, we'll start appending after the last readable offset
            next_lsn_ = 0;
        }
    }
}

WALManager::~WALManager() {
    close();
}

lsn_t WALManager::append_record(txn_id_t txn_id, LogRecordType type, 
                                page_id_t page_id, uint32_t slot_id, 
                                std::vector<uint8_t> before_img, std::vector<uint8_t> after_img) {
    std::lock_guard<std::mutex> lock(log_lck_);
    lsn_t cur_lsn = next_lsn_++;

    std::vector<uint8_t> buf;

    // LSN (8 bytes)
    for (int i = 7; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((cur_lsn >> (i * 8)) & 0xFF));
    }
    // Txn ID (4 bytes)
    for (int i = 3; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((txn_id >> (i * 8)) & 0xFF));
    }
    // Type (4 bytes)
    uint32_t type_val = static_cast<uint32_t>(type);
    for (int i = 3; i >= 0; --i) {
        buf.push_back(static_cast<uint8_t>((type_val >> (i * 8)) & 0xFF));
    }

    bool has_image = (type == LogRecordType::UPDATE || type == LogRecordType::SAVEPOINT || type == LogRecordType::RELEASE_SAVEPOINT || type == LogRecordType::ROLLBACK_TO);
    if (has_image) {
        // Page ID (4 bytes)
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((page_id >> (i * 8)) & 0xFF));
        }
        // Slot ID (4 bytes)
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((slot_id >> (i * 8)) & 0xFF));
        }
        // Before image length (4 bytes) + data
        uint32_t b_len = static_cast<uint32_t>(before_img.size());
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((b_len >> (i * 8)) & 0xFF));
        }
        buf.insert(buf.end(), before_img.begin(), before_img.end());

        // After image length (4 bytes) + data
        uint32_t a_len = static_cast<uint32_t>(after_img.size());
        for (int i = 3; i >= 0; --i) {
            buf.push_back(static_cast<uint8_t>((a_len >> (i * 8)) & 0xFF));
        }
        buf.insert(buf.end(), after_img.begin(), after_img.end());
    }

    // Write to end of log file
    log_io_.seekp(0, std::ios::end);
    log_io_.write(reinterpret_cast<const char*>(buf.data()), buf.size());

    // Flush commits & aborts immediately to ensure transactional integrity
    if (type == LogRecordType::COMMIT || type == LogRecordType::ABORT) {
        log_io_.flush();
    }

    return cur_lsn;
}

void WALManager::flush() {
    std::lock_guard<std::mutex> lock(log_lck_);
    if (log_io_.is_open()) {
        log_io_.flush();
    }
}

std::vector<LogRecord> WALManager::read_all_records() {
    std::lock_guard<std::mutex> lock(log_lck_);
    std::vector<LogRecord> records;

    log_io_.seekg(0, std::ios::end);
    std::streamoff file_size = log_io_.tellg();
    if (file_size <= 0) {
        return records;
    }

    log_io_.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(file_size);
    log_io_.read(reinterpret_cast<char*>(data.data()), file_size);
    if (log_io_.bad()) {
        log_io_.clear();
        throw DBException(StatusCode::ERR_IO_ERROR, "Failed to read entire log file");
    }

    size_t offset = 0;
    while (offset < data.size()) {
        if (offset + 16 > data.size()) {
            // Truncated header
            break;
        }

        LogRecord rec;
        // LSN
        rec.lsn = 0;
        for (int i = 0; i < 8; ++i) {
            rec.lsn = (rec.lsn << 8) | data[offset + i];
        }
        offset += 8;

        // Txn ID
        rec.txn_id = 0;
        for (int i = 0; i < 4; ++i) {
            rec.txn_id = (rec.txn_id << 8) | data[offset + i];
        }
        offset += 4;

        // Type
        uint32_t type_val = 0;
        for (int i = 0; i < 4; ++i) {
            type_val = (type_val << 8) | data[offset + i];
        }
        offset += 4;

        if (type_val > static_cast<uint32_t>(LogRecordType::ROLLBACK_TO)) {
            // Corruption detected
            break;
        }
        rec.type = static_cast<LogRecordType>(type_val);

        bool has_image = (rec.type == LogRecordType::UPDATE || rec.type == LogRecordType::SAVEPOINT || rec.type == LogRecordType::RELEASE_SAVEPOINT || rec.type == LogRecordType::ROLLBACK_TO);
        if (has_image) {
            if (offset + 16 > data.size()) {
                break;
            }

            // Page ID
            rec.page_id = 0;
            for (int i = 0; i < 4; ++i) {
                rec.page_id = (rec.page_id << 8) | data[offset + i];
            }
            offset += 4;

            // Slot ID
            rec.slot_id = 0;
            for (int i = 0; i < 4; ++i) {
                rec.slot_id = (rec.slot_id << 8) | data[offset + i];
            }
            offset += 4;

            // Before image len
            uint32_t b_len = 0;
            for (int i = 0; i < 4; ++i) {
                b_len = (b_len << 8) | data[offset + i];
            }
            offset += 4;

            if (offset + b_len > data.size()) {
                break;
            }
            rec.before_image.insert(rec.before_image.end(), data.begin() + offset, data.begin() + offset + b_len);
            offset += b_len;

            if (offset + 4 > data.size()) {
                break;
            }

            // After image len
            uint32_t a_len = 0;
            for (int i = 0; i < 4; ++i) {
                a_len = (a_len << 8) | data[offset + i];
            }
            offset += 4;

            if (offset + a_len > data.size()) {
                break;
            }
            rec.after_image.insert(rec.after_image.end(), data.begin() + offset, data.begin() + offset + a_len);
            offset += a_len;
        }

        records.push_back(std::move(rec));
    }

    return records;
}

void WALManager::close() {
    std::lock_guard<std::mutex> lock(log_lck_);
    if (log_io_.is_open()) {
        log_io_.close();
    }
}

} // namespace luminadb
