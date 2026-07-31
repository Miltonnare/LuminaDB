#ifndef LUMINADB_STORAGE_DISK_MANAGER_H
#define LUMINADB_STORAGE_DISK_MANAGER_H

#include <string>
#include <fstream>
#include <mutex>
#include "luminadb/common/types.h"
#include "luminadb/common/errors.h"

namespace luminadb {

class DiskManager {
public:
    explicit DiskManager(std::string db_file);
    ~DiskManager();

    // Prevent copy/move
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    void read_page(page_id_t page_id, uint8_t* page_buf);
    void write_page(page_id_t page_id, const uint8_t* page_buf);

    page_id_t allocate_page();
    page_id_t num_pages() const;

    void close();

private:
    std::string file_name_;
    std::fstream db_io_;
    page_id_t num_pages_{0};
    mutable std::mutex db_io_lck_;
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_DISK_MANAGER_H
