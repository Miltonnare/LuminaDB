#include "luminadb/storage/replacer.h"
#include <algorithm>

namespace luminadb {

// ==========================================
// LruReplacer
// ==========================================

LruReplacer::LruReplacer(size_t num_pages) : capacity_(num_pages) {}

bool LruReplacer::victim(frame_id_t& frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (lru_list_.empty()) {
        return false;
    }
    frame_id = lru_list_.back();
    lru_map_.erase(frame_id);
    lru_list_.pop_back();
    return true;
}

void LruReplacer::pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = lru_map_.find(frame_id);
    if (iter != lru_map_.end()) {
        lru_list_.erase(iter->second);
        lru_map_.erase(iter);
    }
}

void LruReplacer::unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (lru_map_.find(frame_id) != lru_map_.end()) {
        return;
    }
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}

size_t LruReplacer::size() {
    std::lock_guard<std::mutex> lock(latch_);
    return lru_list_.size();
}

// ==========================================
// ClockReplacer
// ==========================================

ClockReplacer::ClockReplacer(size_t num_pages) : clock_status_(num_pages) {}

bool ClockReplacer::victim(frame_id_t& frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (size_ == 0) {
        return false;
    }

    while (true) {
        if (clock_status_[hand_].in_replacer) {
            if (clock_status_[hand_].reference_bit) {
                clock_status_[hand_].reference_bit = false;
            } else {
                frame_id = static_cast<frame_id_t>(hand_);
                clock_status_[hand_].in_replacer = false;
                size_--;
                hand_ = (hand_ + 1) % clock_status_.size();
                return true;
            }
        }
        hand_ = (hand_ + 1) % clock_status_.size();
    }
}

void ClockReplacer::pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (static_cast<size_t>(frame_id) >= clock_status_.size()) {
        return;
    }
    if (clock_status_[frame_id].in_replacer) {
        clock_status_[frame_id].in_replacer = false;
        size_--;
    }
}

void ClockReplacer::unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (static_cast<size_t>(frame_id) >= clock_status_.size()) {
        return;
    }
    if (!clock_status_[frame_id].in_replacer) {
        clock_status_[frame_id].in_replacer = true;
        clock_status_[frame_id].reference_bit = true;
        size_++;
    }
}

size_t ClockReplacer::size() {
    std::lock_guard<std::mutex> lock(latch_);
    return size_;
}

// ==========================================
// LfuReplacer
// ==========================================

LfuReplacer::LfuReplacer(size_t num_pages) {}

bool LfuReplacer::victim(frame_id_t& frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    if (in_replacer_.empty()) {
        return false;
    }

    frame_id_t victim_fid = -1;
    int min_freq = std::numeric_limits<int>::max();

    for (frame_id_t fid : in_replacer_) {
        int freq = frequencies_[fid];
        if (freq < min_freq) {
            min_freq = freq;
            victim_fid = fid;
        }
    }

    if (victim_fid != -1) {
        frame_id = victim_fid;
        in_replacer_.erase(victim_fid);
        frequencies_.erase(victim_fid);
        return true;
    }
    return false;
}

void LfuReplacer::pin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    in_replacer_.erase(frame_id);
}

void LfuReplacer::unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    in_replacer_.insert(frame_id);
    frequencies_[frame_id]++;
}

size_t LfuReplacer::size() {
    std::lock_guard<std::mutex> lock(latch_);
    return in_replacer_.size();
}

} // namespace luminadb
