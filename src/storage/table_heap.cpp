#include "luminadb/storage/table_heap.h"
#include <vector>

namespace luminadb {

TableHeap::TableHeap(DiskManager& disk_manager, page_id_t first_page_id)
    : disk_manager_(disk_manager), first_page_id_(first_page_id) {
    if (first_page_id_ == INVALID_PAGE_ID) {
        first_page_id_ = disk_manager_.allocate_page();
        std::vector<uint8_t> page_buf(PAGE_SIZE);
        Page::init(page_buf.data());
        disk_manager_.write_page(first_page_id_, page_buf.data());
    }
}

record_id_t TableHeap::insert_record(const std::vector<uint8_t>& data) {
    page_id_t current_pid = first_page_id_;
    std::vector<uint8_t> page_buf(PAGE_SIZE);

    // Traverse to the last page of this table
    while (true) {
        disk_manager_.read_page(current_pid, page_buf.data());
        page_id_t next_pid = Page::get_next_page_id(page_buf.data());
        if (next_pid == INVALID_PAGE_ID) {
            break;
        }
        current_pid = next_pid;
    }

    // Try to insert on the last page
    int slot_id = Page::insert_record(page_buf.data(), data.data(), data.size());
    if (slot_id >= 0) {
        disk_manager_.write_page(current_pid, page_buf.data());
        return make_record_id(current_pid, static_cast<uint32_t>(slot_id));
    }

    // Last page is full; allocate a new page
    page_id_t new_pid = disk_manager_.allocate_page();
    std::vector<uint8_t> new_page_buf(PAGE_SIZE);
    Page::init(new_page_buf.data());

    slot_id = Page::insert_record(new_page_buf.data(), data.data(), data.size());
    if (slot_id < 0) {
        throw DBException(StatusCode::ERR_INTERNAL, "Failed to insert record into a freshly allocated page");
    }

    // Link the old last page to the new page
    Page::set_next_page_id(page_buf.data(), new_pid);
    disk_manager_.write_page(current_pid, page_buf.data());

    // Write the new page
    disk_manager_.write_page(new_pid, new_page_buf.data());
    return make_record_id(new_pid, static_cast<uint32_t>(slot_id));
}

bool TableHeap::get_record(record_id_t rid, std::vector<uint8_t>& data) const {
    page_id_t page_id = get_page_id(rid);
    uint32_t slot_id = get_slot_id(rid);

    if (page_id < 0 || page_id >= disk_manager_.num_pages()) {
        return false;
    }

    std::vector<uint8_t> page_buf(PAGE_SIZE);
    disk_manager_.read_page(page_id, page_buf.data());

    const uint8_t* rec_ptr = nullptr;
    size_t rec_len = 0;
    if (!Page::get_record(page_buf.data(), slot_id, rec_ptr, rec_len)) {
        return false;
    }

    data.assign(rec_ptr, rec_ptr + rec_len);
    return true;
}

bool TableHeap::update_record(record_id_t rid, const std::vector<uint8_t>& data) {
    page_id_t page_id = get_page_id(rid);
    uint32_t slot_id = get_slot_id(rid);

    if (page_id < 0 || page_id >= disk_manager_.num_pages()) {
        return false;
    }

    std::vector<uint8_t> page_buf(PAGE_SIZE);
    disk_manager_.read_page(page_id, page_buf.data());

    if (!Page::update_record(page_buf.data(), slot_id, data.data(), data.size())) {
        return false; // Not enough space
    }

    disk_manager_.write_page(page_id, page_buf.data());
    return true;
}

bool TableHeap::delete_record(record_id_t rid) {
    page_id_t page_id = get_page_id(rid);
    uint32_t slot_id = get_slot_id(rid);

    if (page_id < 0 || page_id >= disk_manager_.num_pages()) {
        return false;
    }

    std::vector<uint8_t> page_buf(PAGE_SIZE);
    disk_manager_.read_page(page_id, page_buf.data());

    if (!Page::delete_record(page_buf.data(), slot_id)) {
        return false;
    }

    disk_manager_.write_page(page_id, page_buf.data());
    return true;
}

record_id_t TableHeap::get_first_record_id() const {
    page_id_t current_pid = first_page_id_;
    std::vector<uint8_t> page_buf(PAGE_SIZE);

    while (current_pid != INVALID_PAGE_ID) {
        disk_manager_.read_page(current_pid, page_buf.data());
        uint16_t slots = Page::get_slot_count(page_buf.data());
        for (uint32_t sid = 0; sid < slots; ++sid) {
            const uint8_t* dummy_ptr = nullptr;
            size_t dummy_len = 0;
            if (Page::get_record(page_buf.data(), sid, dummy_ptr, dummy_len)) {
                return make_record_id(current_pid, sid);
            }
        }
        current_pid = Page::get_next_page_id(page_buf.data());
    }

    return make_record_id(INVALID_PAGE_ID, 0);
}

record_id_t TableHeap::get_next_record_id(record_id_t rid) const {
    page_id_t page_id = get_page_id(rid);
    uint32_t slot_id = get_slot_id(rid);

    if (page_id == INVALID_PAGE_ID) {
        return make_record_id(INVALID_PAGE_ID, 0);
    }

    std::vector<uint8_t> page_buf(PAGE_SIZE);
    disk_manager_.read_page(page_id, page_buf.data());
    uint16_t slots = Page::get_slot_count(page_buf.data());

    // Search same page
    for (uint32_t sid = slot_id + 1; sid < slots; ++sid) {
        const uint8_t* dummy_ptr = nullptr;
        size_t dummy_len = 0;
        if (Page::get_record(page_buf.data(), sid, dummy_ptr, dummy_len)) {
            return make_record_id(page_id, sid);
        }
    }

    // Search subsequent pages linked
    page_id_t next_pid = Page::get_next_page_id(page_buf.data());
    while (next_pid != INVALID_PAGE_ID) {
        disk_manager_.read_page(next_pid, page_buf.data());
        uint16_t current_slots = Page::get_slot_count(page_buf.data());
        for (uint32_t sid = 0; sid < current_slots; ++sid) {
            const uint8_t* dummy_ptr = nullptr;
            size_t dummy_len = 0;
            if (Page::get_record(page_buf.data(), sid, dummy_ptr, dummy_len)) {
                return make_record_id(next_pid, sid);
            }
        }
        next_pid = Page::get_next_page_id(page_buf.data());
    }

    return make_record_id(INVALID_PAGE_ID, 0);
}

} // namespace luminadb
