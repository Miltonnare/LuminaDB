#ifndef LUMINADB_CONCURRENCY_TRANSACTION_MANAGER_H
#define LUMINADB_CONCURRENCY_TRANSACTION_MANAGER_H

#include <mutex>
#include <unordered_map>
#include <memory>
#include "luminadb/common/types.h"
#include "luminadb/storage/wal.h"
#include "luminadb/concurrency/lock_manager.h"

#include <unordered_set>

namespace luminadb {

enum class TransactionState {
    GROWING,
    SHRINKING,
    COMMITTED,
    ABORTED
};

enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ
};

class Transaction {
public:
    explicit Transaction(txn_id_t txn_id, IsolationLevel iso = IsolationLevel::REPEATABLE_READ) 
        : txn_id_(txn_id), state_(TransactionState::GROWING), isolation_level_(iso) {}

    txn_id_t get_txn_id() const { return txn_id_; }
    TransactionState get_state() const { return state_; }
    void set_state(TransactionState state) { state_ = state; }
    
    IsolationLevel get_isolation_level() const { return isolation_level_; }
    void set_isolation_level(IsolationLevel iso) { isolation_level_ = iso; }

private:
    txn_id_t txn_id_;
    TransactionState state_;
    IsolationLevel isolation_level_;
};

class TransactionManager {
public:
    TransactionManager(LockManager& lock_manager, WALManager& wal_manager);
    ~TransactionManager() = default;

    // Prevent copy/move
    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    Transaction* begin_txn(IsolationLevel iso = IsolationLevel::REPEATABLE_READ);
    void commit(Transaction* txn);
    void abort(Transaction* txn);

    Transaction* get_transaction(txn_id_t txn_id) const;
    bool is_committed(txn_id_t txn_id) const;

private:
    LockManager& lock_manager_;
    WALManager& wal_manager_;
    txn_id_t next_txn_id_{1};
    mutable std::mutex latch_;
    std::unordered_map<txn_id_t, std::unique_ptr<Transaction>> txn_table_;
    std::unordered_set<txn_id_t> committed_txns_;
};

} // namespace luminadb

#endif // LUMINADB_CONCURRENCY_TRANSACTION_MANAGER_H
