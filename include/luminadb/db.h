#ifndef LUMINADB_DB_H
#define LUMINADB_DB_H

#include <string>
#include <memory>
#include <shared_mutex>
#include "luminadb/common/errors.h"
#include "luminadb/core/catalog.h"
#include "luminadb/storage/disk_manager.h"
#include "luminadb/storage/buffer_pool_manager.h"
#include "luminadb/storage/wal.h"
#include "luminadb/concurrency/lock_manager.h"
#include "luminadb/concurrency/transaction_manager.h"
#include "luminadb/query/executor.h"

#include "luminadb/query/query_cache.h"

namespace luminadb {

class DB {
public:
    static std::unique_ptr<DB> open(std::string db_dir, std::string db_name, size_t bpm_pool_size = 10);
    ~DB();

    // Prevent copy/move
    DB(const DB&) = delete;
    DB& operator=(const DB&) = delete;

    // Executes a query SQL string and returns results. Thread-safe.
    QueryResult execute(std::string query_str);

    bool has_catalog() const;

    // Direct access to the query cache for testing
    QueryCache& get_query_cache() { return query_cache_; }

private:
    DB(std::string db_file, std::string log_file, std::string catalog_file, size_t bpm_pool_size);

    void initialize();
    void load_catalog();
    void save_catalog();
    void rebuild_indexes();
    void recover();

    std::string db_file_;
    std::string log_file_;
    std::string catalog_file_;
    size_t bpm_pool_size_;

    std::unique_ptr<DiskManager> disk_manager_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<WALManager> wal_manager_;
    std::unique_ptr<LockManager> lock_manager_;
    std::unique_ptr<TransactionManager> txn_manager_;
    std::unique_ptr<Catalog> catalog_;
    QueryCache query_cache_;
    Transaction* current_txn_{nullptr};
    bool in_savepoint_txn_{false};

    mutable std::shared_mutex db_rw_lck_; // Protects the DB state
};

} // namespace luminadb

#endif // LUMINADB_DB_H
