#include "luminadb/storage/btree_index.h"
#include <algorithm>

namespace luminadb {

BTreeIndex::BTreeIndex() {
    root_ = std::make_unique<Node>(true);
}

BTreeIndex::~BTreeIndex() {
    clear();
}

void BTreeIndex::clear() {
    root_ = std::make_unique<Node>(true);
}

BTreeIndex::Node* BTreeIndex::find_leaf(Node* node, const Value& key) const {
    if (node->is_leaf) {
        return node;
    }

    auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
    int idx = static_cast<int>(std::distance(node->keys.begin(), it));
    return find_leaf(node->children[idx].get(), key);
}

bool BTreeIndex::find(const Value& key, record_id_t& val) const {
    if (!root_) return false;
    Node* leaf = find_leaf(root_.get(), key);
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        int idx = static_cast<int>(std::distance(leaf->keys.begin(), it));
        val = leaf->values[idx];
        return true;
    }
    return false;
}

void BTreeIndex::insert(const Value& key, record_id_t val) {
    std::unique_ptr<Node> split_child;
    Value split_key;
    insert_internal(root_.get(), key, val, split_child, split_key);

    if (split_child) {
        auto new_root = std::make_unique<Node>(false);
        new_root->keys.push_back(split_key);
        new_root->children.push_back(std::move(root_));
        new_root->children.push_back(std::move(split_child));
        root_ = std::move(new_root);
    }
}

void BTreeIndex::insert_internal(Node* node, const Value& key, record_id_t val, 
                                 std::unique_ptr<Node>& split_child, Value& split_key) {
    if (node->is_leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        int idx = static_cast<int>(std::distance(node->keys.begin(), it));
        
        if (it != node->keys.end() && *it == key) {
            // Overwrite existing record
            node->values[idx] = val;
            return;
        }

        node->keys.insert(it, key);
        node->values.insert(node->values.begin() + idx, val);

        if (static_cast<int>(node->keys.size()) >= M) {
            // Split leaf
            int split_idx = static_cast<int>(node->keys.size()) / 2;
            auto new_leaf = std::make_unique<Node>(true);

            new_leaf->keys.assign(node->keys.begin() + split_idx, node->keys.end());
            new_leaf->values.assign(node->values.begin() + split_idx, node->values.end());

            node->keys.erase(node->keys.begin() + split_idx, node->keys.end());
            node->values.erase(node->values.begin() + split_idx, node->values.end());

            new_leaf->next = node->next;
            new_leaf->prev = node;
            if (node->next) {
                node->next->prev = new_leaf.get();
            }
            node->next = new_leaf.get();

            split_key = new_leaf->keys.front();
            split_child = std::move(new_leaf);
        }
    } else {
        auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
        int idx = static_cast<int>(std::distance(node->keys.begin(), it));

        std::unique_ptr<Node> child_split;
        Value child_split_key;
        insert_internal(node->children[idx].get(), key, val, child_split, child_split_key);

        if (child_split) {
            node->keys.insert(node->keys.begin() + idx, child_split_key);
            node->children.insert(node->children.begin() + idx + 1, std::move(child_split));

            if (static_cast<int>(node->keys.size()) >= M) {
                // Split internal node
                int split_idx = static_cast<int>(node->keys.size()) / 2;
                auto new_internal = std::make_unique<Node>(false);

                // For internal node, middle key is promoted, not kept in sibling keys
                split_key = node->keys[split_idx];

                new_internal->keys.assign(node->keys.begin() + split_idx + 1, node->keys.end());
                new_internal->children.reserve(node->children.size() - split_idx - 1);
                for (size_t i = split_idx + 1; i < node->children.size(); ++i) {
                    new_internal->children.push_back(std::move(node->children[i]));
                }

                node->keys.erase(node->keys.begin() + split_idx, node->keys.end());
                node->children.erase(node->children.begin() + split_idx + 1, node->children.end());

                split_child = std::move(new_internal);
            }
        }
    }
}

void BTreeIndex::remove(const Value& key) {
    if (!root_) return;
    Node* leaf = find_leaf(root_.get(), key);
    auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
    if (it != leaf->keys.end() && *it == key) {
        int idx = static_cast<int>(std::distance(leaf->keys.begin(), it));
        leaf->keys.erase(it);
        leaf->values.erase(leaf->values.begin() + idx);
    }
    // Simplistic node underflow handling: we don't trigger complex merge/borrow to avoid 
    // excessive logic that might introduce heap corruptions, keeping standard memory safety.
}

std::vector<record_id_t> BTreeIndex::range_scan(const Value& low, const Value& high) const {
    std::vector<record_id_t> results;
    if (!root_) return results;

    Node* leaf = find_leaf(root_.get(), low);
    while (leaf) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            const auto& k = leaf->keys[i];
            if (k >= low && k <= high) {
                results.push_back(leaf->values[i]);
            } else if (k > high) {
                return results;
            }
        }
        leaf = leaf->next;
    }
    return results;
}

} // namespace luminadb
