#include "luminadb/storage/buffer_pool_manager.h"
#include "luminadb/storage/page.h"

#include <thread>
#include <chrono>

namespace luminadb {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager& disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
    replacer_ = std::make_unique<LruReplacer>(pool_size);
    pool_.resize(pool_size_);
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
    flusher_thread_ = std::thread(&BufferPoolManager::run_flusher_loop, this);
}

BufferPoolManager::~BufferPoolManager() {
    run_flusher_ = false;
    if (flusher_thread_.joinable()) {
        flusher_thread_.join();
    }
    flush_all_pages();
}

void BufferPoolManager::run_flusher_loop() {
    while (run_flusher_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(latch_);
        for (size_t fid = 0; fid < pool_.size(); ++fid) {
            Frame& frame = pool_[fid];
            if (frame.page_id != INVALID_PAGE_ID && frame.is_dirty && frame.pin_count == 0) {
                disk_manager_.write_page(frame.page_id, frame.data);
                frame.is_dirty = false;
            }
        }
    }
}

bool BufferPoolManager::find_unpinned_frame(frame_id_t& frame_id) {
    if (!free_list_.empty()) {
        frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }
    return replacer_->victim(frame_id);
}

uint8_t* BufferPoolManager::fetch_page(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter != page_table_.end()) {
        frame_id_t fid = iter->second;
        pool_[fid].pin_count++;
        replacer_->pin(fid);
        return pool_[fid].data;
    }

    frame_id_t fid;
    if (!find_unpinned_frame(fid)) {
        return nullptr; // No frames available (all pinned)
    }

    Frame& frame = pool_[fid];
    if (frame.page_id != INVALID_PAGE_ID) {
        if (frame.is_dirty) {
            disk_manager_.write_page(frame.page_id, frame.data);
            frame.is_dirty = false;
        }
        page_table_.erase(frame.page_id);
    }

    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.is_dirty = false;
    disk_manager_.read_page(page_id, frame.data);

    page_table_[page_id] = fid;
    replacer_->pin(fid);

    return frame.data;
}

bool BufferPoolManager::unpin_page(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }

    frame_id_t fid = iter->second;
    Frame& frame = pool_[fid];

    if (frame.pin_count <= 0) {
        return false;
    }

    if (is_dirty) {
        frame.is_dirty = true;
    }

    frame.pin_count--;
    if (frame.pin_count == 0) {
        replacer_->unpin(fid);
    }

    return true;
}

bool BufferPoolManager::flush_page(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return false;
    }

    frame_id_t fid = iter->second;
    Frame& frame = pool_[fid];
    if (frame.is_dirty) {
        disk_manager_.write_page(frame.page_id, frame.data);
        frame.is_dirty = false;
    }

    return true;
}

uint8_t* BufferPoolManager::new_page(page_id_t& page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    frame_id_t fid;
    if (!find_unpinned_frame(fid)) {
        return nullptr; // No frames available
    }

    page_id = disk_manager_.allocate_page();

    Frame& frame = pool_[fid];
    if (frame.page_id != INVALID_PAGE_ID) {
        if (frame.is_dirty) {
            disk_manager_.write_page(frame.page_id, frame.data);
            frame.is_dirty = false;
        }
        page_table_.erase(frame.page_id);
    }

    frame.page_id = page_id;
    frame.pin_count = 1;
    frame.is_dirty = false;
    Page::init(frame.data);

    page_table_[page_id] = fid;
    replacer_->pin(fid);

    return frame.data;
}

bool BufferPoolManager::delete_page(page_id_t page_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = page_table_.find(page_id);
    if (iter == page_table_.end()) {
        return true;
    }

    frame_id_t fid = iter->second;
    Frame& frame = pool_[fid];

    if (frame.pin_count > 0) {
        return false; // Cannot delete a pinned page
    }

    if (frame.is_dirty) {
        disk_manager_.write_page(frame.page_id, frame.data);
        frame.is_dirty = false;
    }

    page_table_.erase(frame.page_id);
    replacer_->pin(fid);

    frame.page_id = INVALID_PAGE_ID;
    frame.pin_count = 0;
    frame.is_dirty = false;
    std::memset(frame.data, 0, PAGE_SIZE);

    free_list_.push_back(fid);

    return true;
}

void BufferPoolManager::flush_all_pages() {
    std::lock_guard<std::mutex> lock(latch_);
    for (auto& pair : page_table_) {
        frame_id_t fid = pair.second;
        Frame& frame = pool_[fid];
        if (frame.is_dirty) {
            disk_manager_.write_page(frame.page_id, frame.data);
            frame.is_dirty = false;
        }
    }
}

} // namespace luminadb
