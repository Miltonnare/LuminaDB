#ifndef LUMINADB_STORAGE_BTREE_INDEX_H
#define LUMINADB_STORAGE_BTREE_INDEX_H

#include <vector>
#include <memory>
#include "luminadb/core/value.h"
#include "luminadb/common/types.h"

namespace luminadb {

class BTreeIndex {
public:
    BTreeIndex();
    ~BTreeIndex();

    // Prevent copy/move
    BTreeIndex(const BTreeIndex&) = delete;
    BTreeIndex& operator=(const BTreeIndex&) = delete;

    void insert(const Value& key, record_id_t val);
    bool find(const Value& key, record_id_t& val) const;
    void remove(const Value& key);
    
    // Returns record_ids for keys in the range [low, high] (both inclusive)
    std::vector<record_id_t> range_scan(const Value& low, const Value& high) const;

    void clear();

private:
    static constexpr int M = 4; // Max children for B+Tree node

    struct Node {
        bool is_leaf;
        std::vector<Value> keys;
        
        // If leaf = true
        std::vector<record_id_t> values;
        Node* next{nullptr};
        Node* prev{nullptr};

        // If leaf = false
        std::vector<std::unique_ptr<Node>> children;

        explicit Node(bool leaf) : is_leaf(leaf) {}
    };

    std::unique_ptr<Node> root_;

    // Helpers
    void insert_internal(Node* node, const Value& key, record_id_t val, std::unique_ptr<Node>& split_child, Value& split_key);
    Node* find_leaf(Node* node, const Value& key) const;
    void clear_node(Node* node);
};

} // namespace luminadb

#endif // LUMINADB_STORAGE_BTREE_INDEX_H
