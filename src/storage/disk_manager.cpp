#include "luminadb/storage/disk_manager.h"
#include <iostream>
#include <vector>

namespace luminadb {

DiskManager::DiskManager(std::string db_file) : file_name_(std::move(db_file)) {
    db_io_.open(file_name_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        // Clear flags and create file
        db_io_.clear();
        db_io_.open(file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        if (!db_io_.is_open()) {
            throw DBException(StatusCode::ERR_IO_ERROR, "Failed to open or create database file: " + file_name_);
        }
    }

    db_io_.seekp(0, std::ios::end);
    std::streamoff file_size = db_io_.tellp();
    if (file_size < 0) {
        throw DBException(StatusCode::ERR_IO_ERROR, "Failed to get database file size: " + file_name_);
    }
    num_pages_ = static_cast<page_id_t>(file_size / PAGE_SIZE);
}

DiskManager::~DiskManager() {
    close();
}

void DiskManager::read_page(page_id_t page_id, uint8_t* page_buf) {
    std::lock_guard<std::mutex> lock(db_io_lck_);
    if (page_id < 0 || page_id >= num_pages_) {
        throw DBException(StatusCode::ERR_OUT_OF_BOUNDS, 
            "Read page ID " + std::to_string(page_id) + " out of bounds (num_pages=" + std::to_string(num_pages_) + ")");
    }

    db_io_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    db_io_.read(reinterpret_cast<char*>(page_buf), PAGE_SIZE);
    
    if (db_io_.bad() || db_io_.gcount() != PAGE_SIZE) {
        db_io_.clear();
        throw DBException(StatusCode::ERR_IO_ERROR, "DiskManager: Failed to read page " + std::to_string(page_id));
    }
}

void DiskManager::write_page(page_id_t page_id, const uint8_t* page_buf) {
    std::lock_guard<std::mutex> lock(db_io_lck_);
    if (page_id < 0 || page_id >= num_pages_) {
        throw DBException(StatusCode::ERR_OUT_OF_BOUNDS, 
            "Write page ID " + std::to_string(page_id) + " out of bounds (num_pages=" + std::to_string(num_pages_) + ")");
    }

    db_io_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(page_buf), PAGE_SIZE);
    db_io_.flush();

    if (db_io_.bad()) {
        db_io_.clear();
        throw DBException(StatusCode::ERR_IO_ERROR, "DiskManager: Failed to write page " + std::to_string(page_id));
    }
}

page_id_t DiskManager::allocate_page() {
    std::lock_guard<std::mutex> lock(db_io_lck_);
    page_id_t new_page_id = num_pages_;
    
    // Write an empty block at the end of the file to extend it
    std::vector<uint8_t> empty_page(PAGE_SIZE, 0);
    db_io_.seekp(static_cast<std::streamoff>(new_page_id) * PAGE_SIZE, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(empty_page.data()), PAGE_SIZE);
    db_io_.flush();

    if (db_io_.bad()) {
        db_io_.clear();
        throw DBException(StatusCode::ERR_IO_ERROR, "DiskManager: Failed to allocate/extend page " + std::to_string(new_page_id));
    }

    num_pages_++;
    return new_page_id;
}

page_id_t DiskManager::num_pages() const {
    std::lock_guard<std::mutex> lock(db_io_lck_);
    return num_pages_;
}

void DiskManager::close() {
    std::lock_guard<std::mutex> lock(db_io_lck_);
    if (db_io_.is_open()) {
        db_io_.close();
    }
}

} // namespace luminadb
