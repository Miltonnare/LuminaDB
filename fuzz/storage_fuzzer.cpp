#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include "luminadb/storage/page.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;

    uint8_t page_buf[luminadb::PAGE_SIZE];
    luminadb::Page::init(page_buf);

    size_t offset = 0;
    while (offset + 4 < size) {
        uint8_t action = data[offset++];
        uint8_t slot_id = data[offset++];
        uint16_t len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        offset += 2;

        // Cap len to prevent excessive memory consumption / execution time under fuzz
        len = len % (luminadb::PAGE_SIZE + 10);

        if (action % 3 == 0) {
            // Insert record
            if (offset + len > size) break;
            luminadb::Page::insert_record(page_buf, data + offset, len);
            offset += len;
        } else if (action % 3 == 1) {
            // Update record
            if (offset + len > size) break;
            luminadb::Page::update_record(page_buf, slot_id, data + offset, len);
            offset += len;
        } else {
            // Delete record
            luminadb::Page::delete_record(page_buf, slot_id);
        }

        // Periodically defragment to exercise defrag code path
        if (action % 7 == 0) {
            luminadb::Page::defragment(page_buf);
        }
    }

    return 0;
}
