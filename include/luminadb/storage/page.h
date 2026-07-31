#ifndef LUMINADB_STORAGE_PAGE_H
#define LUMINADB_STORAGE_PAGE_H

#include <cstdint>
#include <cstring>
#include <vector>
#include "luminadb/common/types.h"
#include "luminadb/common/errors.h"

namespace luminadb {

class Page {
public:
    static constexpr size_t HEADER_SIZE = 16; // 8 bytes lsn, 4 bytes next_page_id, 2 bytes slot_count, 2 bytes free_space_pointer

    static lsn_t get_lsn(const uint8_t* page_buf) {
        lsn_t lsn;
        std::memcpy(&lsn, page_buf, sizeof(lsn_t));
        return lsn;
    }

    static void set_lsn(uint8_t* page_buf, lsn_t lsn) {
        std::memcpy(page_buf, &lsn, sizeof(lsn_t));
    }

    static page_id_t get_next_page_id(const uint8_t* page_buf) {
        page_id_t next_pid;
        std::memcpy(&next_pid, page_buf + 8, sizeof(page_id_t));
        return next_pid;
    }

    static void set_next_page_id(uint8_t* page_buf, page_id_t next_pid) {
        std::memcpy(page_buf + 8, &next_pid, sizeof(page_id_t));
    }

    static uint16_t get_slot_count(const uint8_t* page_buf) {
        uint16_t count;
        std::memcpy(&count, page_buf + 12, sizeof(uint16_t));
        return count;
    }

    static void set_slot_count(uint8_t* page_buf, uint16_t count) {
        std::memcpy(page_buf + 12, &count, sizeof(uint16_t));
    }

    static uint16_t get_free_space_pointer(const uint8_t* page_buf) {
        uint16_t ptr;
        std::memcpy(&ptr, page_buf + 14, sizeof(uint16_t));
        return ptr;
    }

    static void set_free_space_pointer(uint8_t* page_buf, uint16_t ptr) {
        std::memcpy(page_buf + 14, &ptr, sizeof(uint16_t));
    }

    static void init(uint8_t* page_buf) {
        std::memset(page_buf, 0, PAGE_SIZE);
        set_lsn(page_buf, INVALID_LSN);
        set_next_page_id(page_buf, INVALID_PAGE_ID);
        set_slot_count(page_buf, 0);
        set_free_space_pointer(page_buf, static_cast<uint16_t>(PAGE_SIZE));
    }

    static uint16_t get_free_space(const uint8_t* page_buf) {
        uint16_t slot_count = get_slot_count(page_buf);
        uint16_t free_ptr = get_free_space_pointer(page_buf);
        uint16_t slot_dir_end = static_cast<uint16_t>(HEADER_SIZE + (slot_count * 4));
        if (free_ptr < slot_dir_end) {
            return 0;
        }
        return free_ptr - slot_dir_end;
    }

    // Defragments the slotted page to reclaim space from deleted records
    static void defragment(uint8_t* page_buf) {
        uint16_t slot_count = get_slot_count(page_buf);
        uint16_t free_ptr = static_cast<uint16_t>(PAGE_SIZE);
        std::vector<uint8_t> temp(PAGE_SIZE);

        // Copy original header and slot directory
        std::memcpy(temp.data(), page_buf, HEADER_SIZE + slot_count * 4);

        for (uint16_t i = 0; i < slot_count; ++i) {
            size_t slot_offset_pos = HEADER_SIZE + i * 4;
            uint16_t offset, length;
            std::memcpy(&offset, page_buf + slot_offset_pos, 2);
            std::memcpy(&length, page_buf + slot_offset_pos + 2, 2);

            if (length > 0 && offset > 0 && offset + length <= PAGE_SIZE) {
                free_ptr -= length;
                std::memcpy(temp.data() + free_ptr, page_buf + offset, length);
                std::memcpy(temp.data() + slot_offset_pos, &free_ptr, 2);
            } else {
                // Mark as deleted in slot directory
                uint16_t zero = 0;
                std::memcpy(temp.data() + slot_offset_pos, &zero, 2);
                std::memcpy(temp.data() + slot_offset_pos + 2, &zero, 2);
            }
        }

        std::memcpy(temp.data() + 10, &free_ptr, 2);
        std::memcpy(page_buf, temp.data(), PAGE_SIZE);
    }

    // Returns slot ID on success, or -1 if no space even after defragmentation
    static int insert_record(uint8_t* page_buf, const uint8_t* rec_data, size_t rec_len) {
        if (rec_len > PAGE_SIZE - HEADER_SIZE - 4) {
            return -1; // Record too large for page
        }

        uint16_t slot_count = get_slot_count(page_buf);
        uint16_t free_space = get_free_space(page_buf);

        if (free_space < 4 + rec_len) {
            // Try defragmentation to recover space
            defragment(page_buf);
            free_space = get_free_space(page_buf);
            if (free_space < 4 + rec_len) {
                return -1; // Truly out of space
            }
        }

        uint16_t free_ptr = get_free_space_pointer(page_buf);
        uint16_t new_free_ptr = free_ptr - static_cast<uint16_t>(rec_len);

        std::memcpy(page_buf + new_free_ptr, rec_data, rec_len);

        uint16_t slot_offset = new_free_ptr;
        uint16_t slot_len = static_cast<uint16_t>(rec_len);

        size_t slot_pos = HEADER_SIZE + slot_count * 4;
        std::memcpy(page_buf + slot_pos, &slot_offset, 2);
        std::memcpy(page_buf + slot_pos + 2, &slot_len, 2);

        set_slot_count(page_buf, slot_count + 1);
        set_free_space_pointer(page_buf, new_free_ptr);

        return slot_count;
    }

    // Fetches record data offset and length. Returns false if slot deleted/invalid.
    static bool get_record(const uint8_t* page_buf, uint32_t slot_id, const uint8_t*& rec_data, size_t& rec_len) {
        uint16_t slot_count = get_slot_count(page_buf);
        if (slot_id >= slot_count) {
            return false;
        }

        size_t slot_pos = HEADER_SIZE + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, page_buf + slot_pos, 2);
        std::memcpy(&length, page_buf + slot_pos + 2, 2);

        if (length == 0 || offset == 0 || offset + length > PAGE_SIZE) {
            return false; // Deleted or corrupt
        }

        rec_data = page_buf + offset;
        rec_len = length;
        return true;
    }

    static bool update_record(uint8_t* page_buf, uint32_t slot_id, const uint8_t* rec_data, size_t rec_len) {
        uint16_t slot_count = get_slot_count(page_buf);
        if (slot_id >= slot_count) {
            return false;
        }

        size_t slot_pos = HEADER_SIZE + slot_id * 4;
        uint16_t offset, length;
        std::memcpy(&offset, page_buf + slot_pos, 2);
        std::memcpy(&length, page_buf + slot_pos + 2, 2);

        if (offset > 0 && length > 0 && rec_len <= length) {
            std::memcpy(page_buf + offset, rec_data, rec_len);
            uint16_t new_len = static_cast<uint16_t>(rec_len);
            std::memcpy(page_buf + slot_pos + 2, &new_len, 2);
            return true;
        }

        // Check if there is enough space to write it as a new insertion on this page
        uint16_t free_space = get_free_space(page_buf);
        if (free_space < rec_len) {
            defragment(page_buf);
            free_space = get_free_space(page_buf);
            if (free_space < rec_len) {
                return false; // Insufficient space
            }
        }

        // We write the new version at the free space pointer
        uint16_t free_ptr = get_free_space_pointer(page_buf);
        uint16_t new_free_ptr = free_ptr - static_cast<uint16_t>(rec_len);
        std::memcpy(page_buf + new_free_ptr, rec_data, rec_len);

        // Update the slot directory for slot_id
        uint16_t new_offset = new_free_ptr;
        uint16_t new_len = static_cast<uint16_t>(rec_len);
        std::memcpy(page_buf + slot_pos, &new_offset, 2);
        std::memcpy(page_buf + slot_pos + 2, &new_len, 2);

        set_free_space_pointer(page_buf, new_free_ptr);
        return true;
    }

    static bool delete_record(uint8_t* page_buf, uint32_t slot_id) {
        uint16_t slot_count = get_slot_count(page_buf);
        if (slot_id >= slot_count) {
            return false;
        }

        size_t slot_pos = HEADER_SIZE + slot_id * 4;
        uint16_t zero = 0;
        std::memcpy(page_buf + slot_pos, &zero, 2);
        std::memcpy(page_buf + slot_pos + 2, &zero, 2);
        return true;
    }
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_PAGE_H
