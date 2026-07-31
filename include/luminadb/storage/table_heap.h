#ifndef LUMINADB_STORAGE_TABLE_HEAP_H
#define LUMINADB_STORAGE_TABLE_HEAP_H

#include <vector>
#include "luminadb/common/types.h"
#include "luminadb/storage/disk_manager.h"
#include "luminadb/storage/page.h"

namespace luminadb {

class TableHeap {
public:
    explicit TableHeap(DiskManager& disk_manager, page_id_t first_page_id = INVALID_PAGE_ID);

    // Prevent copy/move
    TableHeap(const TableHeap&) = delete;
    TableHeap& operator=(const TableHeap&) = delete;

    // Inserts a raw record buffer. Returns the record_id_t.
    record_id_t insert_record(const std::vector<uint8_t>& data);

    // Fetches the record data. Returns false if deleted or invalid.
    bool get_record(record_id_t rid, std::vector<uint8_t>& data) const;

    // Updates the record in place. Returns false if space is insufficient.
    bool update_record(record_id_t rid, const std::vector<uint8_t>& data);

    // Marks the record as deleted.
    bool delete_record(record_id_t rid);

    // Iterator support
    record_id_t get_first_record_id() const;
    record_id_t get_next_record_id(record_id_t rid) const;

    page_id_t get_first_page_id() const { return first_page_id_; }

private:
    DiskManager& disk_manager_;
    page_id_t first_page_id_;
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_TABLE_HEAP_H
