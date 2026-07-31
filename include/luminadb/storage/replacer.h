#ifndef LUMINADB_STORAGE_REPLACER_H
#define LUMINADB_STORAGE_REPLACER_H

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <vector>
#include "luminadb/common/types.h"

namespace luminadb {

class AbstractReplacer {
public:
    virtual ~AbstractReplacer() = default;
    virtual bool victim(frame_id_t& frame_id) = 0;
    virtual void pin(frame_id_t frame_id) = 0;
    virtual void unpin(frame_id_t frame_id) = 0;
    virtual size_t size() = 0;
};

class LruReplacer : public AbstractReplacer {
public:
    explicit LruReplacer(size_t num_pages);
    ~LruReplacer() override = default;

    bool victim(frame_id_t& frame_id) override;
    void pin(frame_id_t frame_id) override;
    void unpin(frame_id_t frame_id) override;
    size_t size() override;

private:
    std::mutex latch_;
    std::list<frame_id_t> lru_list_;
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    size_t capacity_;
};

class ClockReplacer : public AbstractReplacer {
public:
    explicit ClockReplacer(size_t num_pages);
    ~ClockReplacer() override = default;

    bool victim(frame_id_t& frame_id) override;
    void pin(frame_id_t frame_id) override;
    void unpin(frame_id_t frame_id) override;
    size_t size() override;

private:
    std::mutex latch_;
    struct ClockStatus {
        bool in_replacer{false};
        bool reference_bit{false};
    };
    std::vector<ClockStatus> clock_status_;
    size_t hand_{0};
    size_t size_{0};
};

class LfuReplacer : public AbstractReplacer {
public:
    explicit LfuReplacer(size_t num_pages);
    ~LfuReplacer() override = default;

    bool victim(frame_id_t& frame_id) override;
    void pin(frame_id_t frame_id) override;
    void unpin(frame_id_t frame_id) override;
    size_t size() override;

private:
    std::mutex latch_;
    std::unordered_map<frame_id_t, int> frequencies_;
    std::unordered_set<frame_id_t> in_replacer_;
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_REPLACER_H
