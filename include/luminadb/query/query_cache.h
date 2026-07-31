#ifndef LUMINADB_QUERY_QUERY_CACHE_H
#define LUMINADB_QUERY_QUERY_CACHE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "luminadb/query/executor.h"

namespace luminadb {

struct CacheEntry {
    std::vector<std::string> column_names;
    std::vector<DataType> column_types;
    std::vector<std::vector<Value>> rows;
    std::string message;
    std::vector<std::string> referenced_tables;
};

class QueryCache {
public:
    QueryCache() = default;
    
    bool lookup(const std::string& query, QueryResult& result) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(query);
        if (it == cache_.end()) return false;
        
        result.column_names = it->second.column_names;
        result.column_types = it->second.column_types;
        result.rows = it->second.rows;
        result.message = it->second.message;
        return true;
    }
    
    void put(const std::string& query, const QueryResult& result, const std::vector<std::string>& referenced_tables) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        CacheEntry entry;
        entry.column_names = result.column_names;
        entry.column_types = result.column_types;
        entry.rows = result.rows;
        entry.message = result.message;
        entry.referenced_tables = referenced_tables;
        
        cache_[query] = entry;
        for (const auto& tbl : referenced_tables) {
            table_to_queries_[tbl].insert(query);
        }
    }
    
    void invalidate_table(const std::string& table_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = table_to_queries_.find(table_name);
        if (it != table_to_queries_.end()) {
            for (const auto& query : it->second) {
                cache_.erase(query);
            }
            table_to_queries_.erase(it);
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_.clear();
        table_to_queries_.clear();
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return cache_.size();
    }
    
private:
    std::unordered_map<std::string, CacheEntry> cache_;
    std::unordered_map<std::string, std::unordered_set<std::string>> table_to_queries_;
    mutable std::mutex mutex_;
};

} // namespace luminadb

#endif // LUMINADB_QUERY_QUERY_CACHE_H
