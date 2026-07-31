#include "luminadb/concurrency/transaction_manager.h"

namespace luminadb {

TransactionManager::TransactionManager(LockManager& lock_manager, WALManager& wal_manager)
    : lock_manager_(lock_manager), wal_manager_(wal_manager) {}

Transaction* TransactionManager::begin_txn(IsolationLevel iso) {
    std::lock_guard<std::mutex> lock(latch_);
    txn_id_t txn_id = next_txn_id_++;
    
    // Write BEGIN record to WAL
    wal_manager_.append_record(txn_id, LogRecordType::BEGIN);

    auto txn = std::make_unique<Transaction>(txn_id, iso);
    Transaction* raw_ptr = txn.get();
    txn_table_[txn_id] = std::move(txn);
    
    return raw_ptr;
}

void TransactionManager::commit(Transaction* txn) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!txn || txn->get_state() == TransactionState::COMMITTED || txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    txn->set_state(TransactionState::COMMITTED);
    committed_txns_.insert(txn->get_txn_id());
    
    // Write COMMIT to WAL and force flush
    wal_manager_.append_record(txn->get_txn_id(), LogRecordType::COMMIT);
    wal_manager_.flush();

    // Release all held locks
    lock_manager_.release_all_locks(txn->get_txn_id());
}

void TransactionManager::abort(Transaction* txn) {
    std::lock_guard<std::mutex> lock(latch_);
    if (!txn || txn->get_state() == TransactionState::COMMITTED || txn->get_state() == TransactionState::ABORTED) {
        return;
    }

    txn->set_state(TransactionState::ABORTED);

    // Write ABORT to WAL and force flush
    wal_manager_.append_record(txn->get_txn_id(), LogRecordType::ABORT);
    wal_manager_.flush();

    // Release all held locks
    lock_manager_.release_all_locks(txn->get_txn_id());
}

Transaction* TransactionManager::get_transaction(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(latch_);
    auto iter = txn_table_.find(txn_id);
    if (iter == txn_table_.end()) {
        return nullptr;
    }
    return iter->second.get();
}

bool TransactionManager::is_committed(txn_id_t txn_id) const {
    std::lock_guard<std::mutex> lock(latch_);
    return committed_txns_.find(txn_id) != committed_txns_.end();
}

} // namespace luminadb
