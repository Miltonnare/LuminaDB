#include "luminadb/concurrency/lock_manager.h"
#include <chrono>
#include <algorithm>

namespace luminadb {

LockManager::LockManager() {
    deadlock_detection_thread_ = std::thread(&LockManager::run_deadlock_detection_loop, this);
}

LockManager::~LockManager() {
    run_deadlock_detection_ = false;
    if (deadlock_detection_thread_.joinable()) {
        deadlock_detection_thread_.join();
    }
}

bool LockManager::acquire_shared(txn_id_t txn_id, record_id_t rid) {
    std::unique_lock<std::mutex> lock(latch_);

    if (aborted_txns_.count(txn_id) > 0) {
        return false;
    }

    // Check if txn already holds the lock on this record
    if (txn_locks_[txn_id].count(rid) > 0) {
        auto& queue = lock_table_[rid];
        for (const auto& req : queue.requests) {
            if (req.txn_id == txn_id && req.granted) {
                return true;
            }
        }
    }

    auto& queue = lock_table_[rid];
    queue.requests.push_back(LockRequest{txn_id, LockMode::SHARED, false});
    txn_locks_[txn_id].insert(rid);

    auto can_grant_shared = [&]() -> bool {
        for (const auto& req : queue.requests) {
            if (req.txn_id == txn_id) {
                return true;
            }
            if (req.mode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        return true;
    };

    while (!can_grant_shared()) {
        if (aborted_txns_.count(txn_id) > 0) {
            for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
                if (it->txn_id == txn_id) {
                    queue.requests.erase(it);
                    break;
                }
            }
            txn_locks_[txn_id].erase(rid);
            return false;
        }

        if (queue.cv.wait_for(lock, std::chrono::milliseconds(50)) == std::cv_status::timeout) {
            if (check_deadlock_and_abort(txn_id)) {
                for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
                    if (it->txn_id == txn_id) {
                        queue.requests.erase(it);
                        break;
                    }
                }
                txn_locks_[txn_id].erase(rid);
                aborted_txns_.insert(txn_id);
                return false;
            }
        }
    }

    // Grant lock
    for (auto& req : queue.requests) {
        if (req.txn_id == txn_id) {
            req.granted = true;
            queue.shared_count++;
            break;
        }
    }

    return true;
}

bool LockManager::acquire_exclusive(txn_id_t txn_id, record_id_t rid) {
    std::unique_lock<std::mutex> lock(latch_);

    if (aborted_txns_.count(txn_id) > 0) {
        return false;
    }

    // Check if txn already holds exclusive lock
    if (txn_locks_[txn_id].count(rid) > 0) {
        auto& queue = lock_table_[rid];
        for (const auto& req : queue.requests) {
            if (req.txn_id == txn_id && req.mode == LockMode::EXCLUSIVE && req.granted) {
                return true;
            }
        }
    }

    auto& queue = lock_table_[rid];
    queue.requests.push_back(LockRequest{txn_id, LockMode::EXCLUSIVE, false});
    txn_locks_[txn_id].insert(rid);

    auto can_grant_exclusive = [&]() -> bool {
        return queue.requests.front().txn_id == txn_id;
    };

    while (!can_grant_exclusive()) {
        if (aborted_txns_.count(txn_id) > 0) {
            for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
                if (it->txn_id == txn_id) {
                    queue.requests.erase(it);
                    break;
                }
            }
            txn_locks_[txn_id].erase(rid);
            return false;
        }

        if (queue.cv.wait_for(lock, std::chrono::milliseconds(50)) == std::cv_status::timeout) {
            if (check_deadlock_and_abort(txn_id)) {
                for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
                    if (it->txn_id == txn_id) {
                        queue.requests.erase(it);
                        break;
                    }
                }
                txn_locks_[txn_id].erase(rid);
                aborted_txns_.insert(txn_id);
                return false;
            }
        }
    }

    // Grant lock
    for (auto& req : queue.requests) {
        if (req.txn_id == txn_id) {
            req.granted = true;
            queue.is_writing = true;
            break;
        }
    }

    return true;
}

bool LockManager::release(txn_id_t txn_id, record_id_t rid) {
    std::lock_guard<std::mutex> lock(latch_);

    auto iter = lock_table_.find(rid);
    if (iter == lock_table_.end()) {
        return false;
    }

    auto& queue = iter->second;
    for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
        if (it->txn_id == txn_id && it->granted) {
            if (it->mode == LockMode::SHARED) {
                queue.shared_count--;
            } else {
                queue.is_writing = false;
            }
            queue.requests.erase(it);
            break;
        }
    }

    txn_locks_[txn_id].erase(rid);
    queue.cv.notify_all();
    return true;
}

void LockManager::release_all_locks(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(latch_);

    auto locks = txn_locks_[txn_id];
    for (record_id_t rid : locks) {
        auto iter = lock_table_.find(rid);
        if (iter != lock_table_.end()) {
            auto& queue = iter->second;
            for (auto it = queue.requests.begin(); it != queue.requests.end(); ++it) {
                if (it->txn_id == txn_id) {
                    if (it->granted) {
                        if (it->mode == LockMode::SHARED) {
                            queue.shared_count--;
                        } else {
                            queue.is_writing = false;
                        }
                    }
                    queue.requests.erase(it);
                    break;
                }
            }
            queue.cv.notify_all();
        }
    }

    txn_locks_.erase(txn_id);
}

bool LockManager::is_txn_aborted(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(latch_);
    return aborted_txns_.count(txn_id) > 0;
}

void LockManager::clear_aborted_txn(txn_id_t txn_id) {
    std::lock_guard<std::mutex> lock(latch_);
    aborted_txns_.erase(txn_id);
}

void LockManager::run_deadlock_detection_loop() {
    while (run_deadlock_detection_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::unique_lock<std::mutex> lock(latch_);

        std::unordered_map<txn_id_t, std::vector<txn_id_t>> adj;
        std::unordered_set<txn_id_t> active_txns;

        for (const auto& pair : lock_table_) {
            const auto& queue = pair.second;
            std::vector<txn_id_t> holders;
            std::vector<txn_id_t> waiters;

            for (const auto& req : queue.requests) {
                active_txns.insert(req.txn_id);
                if (req.granted) {
                    holders.push_back(req.txn_id);
                } else {
                    waiters.push_back(req.txn_id);
                }
            }

            for (txn_id_t waiter : waiters) {
                for (txn_id_t holder : holders) {
                    adj[waiter].push_back(holder);
                }
            }
        }

        for (txn_id_t start_txn : active_txns) {
            std::unordered_set<txn_id_t> visited;
            std::unordered_set<txn_id_t> on_path;
            if (dfs_cycle(start_txn, adj, visited, on_path)) {
                aborted_txns_.insert(start_txn);
                
                // Release victim's waiting requests
                for (auto& pair : lock_table_) {
                    auto& queue = pair.second;
                    for (auto it = queue.requests.begin(); it != queue.requests.end(); ) {
                        if (it->txn_id == start_txn && !it->granted) {
                            it = queue.requests.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    queue.cv.notify_all();
                }
                break;
            }
        }
    }
}

bool LockManager::check_deadlock_and_abort(txn_id_t txn_id) {
    std::unordered_map<txn_id_t, std::vector<txn_id_t>> adj;

    for (const auto& pair : lock_table_) {
        const auto& queue = pair.second;
        std::vector<txn_id_t> holders;
        std::vector<txn_id_t> waiters;

        for (const auto& req : queue.requests) {
            if (req.granted) {
                holders.push_back(req.txn_id);
            } else {
                waiters.push_back(req.txn_id);
            }
        }

        for (txn_id_t waiter : waiters) {
            for (txn_id_t holder : holders) {
                adj[waiter].push_back(holder);
            }
        }
    }

    std::unordered_set<txn_id_t> visited;
    std::unordered_set<txn_id_t> on_path;
    return dfs_cycle(txn_id, adj, visited, on_path);
}

bool LockManager::dfs_cycle(txn_id_t node, 
                           const std::unordered_map<txn_id_t, std::vector<txn_id_t>>& adj,
                           std::unordered_set<txn_id_t>& visited, 
                           std::unordered_set<txn_id_t>& on_path) {
    visited.insert(node);
    on_path.insert(node);

    auto iter = adj.find(node);
    if (iter != adj.end()) {
        for (txn_id_t neighbor : iter->second) {
            if (on_path.count(neighbor) > 0) {
                return true; // Cycle found
            }
            if (visited.count(neighbor) == 0) {
                if (dfs_cycle(neighbor, adj, visited, on_path)) {
                    return true;
                }
            }
        }
    }

    on_path.erase(node);
    return false;
}

} // namespace luminadb
