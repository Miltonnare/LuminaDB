#ifndef LUMINADB_STORAGE_BUFFER_POOL_MANAGER_H
#define LUMINADB_STORAGE_BUFFER_POOL_MANAGER_H

#include <unordered_map>
#include <mutex>
#include <vector>
#include <list>
#include "luminadb/common/types.h"
#include "luminadb/storage/disk_manager.h"
#include "luminadb/storage/replacer.h"

namespace luminadb {

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager& disk_manager);
    ~BufferPoolManager();

    // Prevent copy/move
    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    // Fetch the requested page from the buffer pool.
    uint8_t* fetch_page(page_id_t page_id);

    // Unpin the target page from the buffer pool.
    bool unpin_page(page_id_t page_id, bool is_dirty);

    // Flush the target page to disk.
    bool flush_page(page_id_t page_id);

    // Create a new page in the buffer pool. Returns pointer to raw bytes and page_id.
    uint8_t* new_page(page_id_t& page_id);

    // Delete a page from the buffer pool and disk.
    bool delete_page(page_id_t page_id);

    // Flush all pages to disk.
    void flush_all_pages();

    size_t get_pool_size() const { return pool_size_; }

private:
    struct Frame {
        page_id_t page_id{INVALID_PAGE_ID};
        int pin_count{0};
        bool is_dirty{false};
        uint8_t data[PAGE_SIZE]{0};
    };

    size_t pool_size_;
    DiskManager& disk_manager_;
    std::vector<Frame> pool_;
    std::unordered_map<page_id_t, frame_id_t> page_table_;
    std::unique_ptr<AbstractReplacer> replacer_;
    std::list<frame_id_t> free_list_;
    std::mutex latch_;

    std::thread flusher_thread_;
    std::atomic<bool> run_flusher_{true};

    void run_flusher_loop();

    bool find_unpinned_frame(frame_id_t& frame_id);
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_BUFFER_POOL_MANAGER_H
