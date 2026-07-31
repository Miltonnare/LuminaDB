#ifndef LUMINADB_CONCURRENCY_LOCK_MANAGER_H
#define LUMINADB_CONCURRENCY_LOCK_MANAGER_H

#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <atomic>
#include "luminadb/common/types.h"
#include "luminadb/common/errors.h"

namespace luminadb {

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

struct LockRequest {
    txn_id_t txn_id;
    LockMode mode;
    bool granted{false};
};

struct LockRequestQueue {
    std::vector<LockRequest> requests;
    std::condition_variable cv;
    bool is_writing{false};
    int shared_count{0};
};

class LockManager {
public:
    LockManager();
    ~LockManager();

    // Prevent copy/move
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    bool acquire_shared(txn_id_t txn_id, record_id_t rid);
    bool acquire_exclusive(txn_id_t txn_id, record_id_t rid);
    bool release(txn_id_t txn_id, record_id_t rid);

    void release_all_locks(txn_id_t txn_id);

    bool is_txn_aborted(txn_id_t txn_id) const;
    void clear_aborted_txn(txn_id_t txn_id);

private:
    mutable std::mutex latch_;
    std::unordered_map<record_id_t, LockRequestQueue> lock_table_;
    std::unordered_map<txn_id_t, std::unordered_set<record_id_t>> txn_locks_;
    std::unordered_set<txn_id_t> aborted_txns_;

    std::thread deadlock_detection_thread_;
    std::atomic<bool> run_deadlock_detection_{true};

    void run_deadlock_detection_loop();

    // Helper functions for cycle detection (deadlock detection)
    bool check_deadlock_and_abort(txn_id_t txn_id);
    bool dfs_cycle(txn_id_t node, 
                  const std::unordered_map<txn_id_t, std::vector<txn_id_t>>& adj,
                  std::unordered_set<txn_id_t>& visited, 
                  std::unordered_set<txn_id_t>& on_path);
};

} // namespace luminadb

#endif // LUMINADB_CONCURRENCY_LOCK_MANAGER_H
