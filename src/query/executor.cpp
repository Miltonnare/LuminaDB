#include "luminadb/query/executor.h"
#include <algorithm>
#include <chrono>
#include "luminadb/concurrency/transaction_manager.h"
#include "luminadb/core/catalog.h"

namespace luminadb {

static Value eval_expr(const Expression* expr, const Schema& schema, const std::vector<Value>& row) {
    if (!expr) {
        return Value(static_cast<int64_t>(1)); // true
    }

    switch (expr->type) {
        case ExpressionType::LITERAL:
            return expr->literal_val;
        case ExpressionType::COL_REF: {
            int idx = schema.get_col_index(expr->col_name);
            if (idx < 0) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Column not found: " + expr->col_name);
            }
            return row[idx];
        }
        case ExpressionType::COMPARE_EQ: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l == r ? 1 : 0));
        }
        case ExpressionType::COMPARE_NEQ: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l != r ? 1 : 0));
        }
        case ExpressionType::COMPARE_LT: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l < r ? 1 : 0));
        }
        case ExpressionType::COMPARE_LTE: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l <= r ? 1 : 0));
        }
        case ExpressionType::COMPARE_GT: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l > r ? 1 : 0));
        }
        case ExpressionType::COMPARE_GTE: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(l >= r ? 1 : 0));
        }
        case ExpressionType::LOGICAL_AND: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>((!l.is_null() && l.as_int() != 0 && !r.is_null() && r.as_int() != 0) ? 1 : 0));
        }
        case ExpressionType::LOGICAL_OR: {
            Value l = eval_expr(expr->left.get(), schema, row);
            Value r = eval_expr(expr->right.get(), schema, row);
            return Value(static_cast<int64_t>(((!l.is_null() && l.as_int() != 0) || (!r.is_null() && r.as_int() != 0)) ? 1 : 0));
        }
        case ExpressionType::LOGICAL_NOT: {
            Value l = eval_expr(expr->left.get(), schema, row);
            return Value(static_cast<int64_t>((!l.is_null() && l.as_int() == 0) ? 1 : 0));
        }
    }
    return Value(static_cast<int64_t>(0));
}

bool Executor::evaluate_expression(const Expression* expr, const Schema& schema, const std::vector<Value>& row) {
    if (!expr) return true;
    try {
        Value val = eval_expr(expr, schema, row);
        if (val.is_null()) return false;
        if (val.type() == DataType::INT) {
            return val.as_int() != 0;
        }
        return false;
    } catch (...) {
        return false;
    }
}

// Check if WHERE clause is index-friendly (i.e. pk = constant)
static bool check_index_scan(const Expression* expr, const Schema& schema, Value& out_key) {
    if (!expr) return false;
    if (expr->type != ExpressionType::COMPARE_EQ) return false;
    
    // Check Left = Column and Right = Literal
    if (expr->left && expr->left->type == ExpressionType::COL_REF &&
        expr->right && expr->right->type == ExpressionType::LITERAL) {
        
        int idx = schema.get_col_index(expr->left->col_name);
        if (idx >= 0 && schema.get_col(idx).is_primary_key) {
            out_key = expr->right->literal_val;
            return true;
        }
    }

    // Check Right = Column and Left = Literal
    if (expr->right && expr->right->type == ExpressionType::COL_REF &&
        expr->left && expr->left->type == ExpressionType::LITERAL) {
        
        int idx = schema.get_col_index(expr->right->col_name);
        if (idx >= 0 && schema.get_col(idx).is_primary_key) {
            out_key = expr->left->literal_val;
            return true;
        }
    }

    return false;
}

QueryResult Executor::execute_select(const SelectStatement& stmt, 
                                      const Schema& schema, 
                                      const TableHeap& table, 
                                      const BTreeIndex& index) {
    QueryResult res;

    // Build projection indices
    std::vector<int> proj_indices;
    if (stmt.fields.empty()) {
        for (size_t i = 0; i < schema.columns().size(); ++i) {
            proj_indices.push_back(static_cast<int>(i));
            res.column_names.push_back(schema.get_col(i).name);
            res.column_types.push_back(schema.get_col(i).type);
        }
    } else {
        for (const auto& field : stmt.fields) {
            int idx = schema.get_col_index(field);
            if (idx < 0) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Column not found in SELECT: " + field);
            }
            proj_indices.push_back(idx);
            res.column_names.push_back(schema.get_col(idx).name);
            res.column_types.push_back(schema.get_col(idx).type);
        }
    }

    // Check for optimized index scan
    Value lookup_key;
    bool is_index = check_index_scan(stmt.where_clause.get(), schema, lookup_key);
    
    std::vector<record_id_t> rids_to_scan;
    if (is_index) {
        record_id_t rid;
        if (index.find(lookup_key, rid)) {
            rids_to_scan.push_back(rid);
        }
    } else {
        // Fallback to sequential scan
        record_id_t rid = table.get_first_record_id();
        while (get_page_id(rid) != INVALID_PAGE_ID) {
            rids_to_scan.push_back(rid);
            rid = table.get_next_record_id(rid);
        }
    }

    for (record_id_t rid : rids_to_scan) {
        std::vector<uint8_t> rec_data;
        if (table.get_record(rid, rec_data)) {
            std::vector<Value> row = schema.deserialize_row(rec_data.data(), rec_data.size());
            if (evaluate_expression(stmt.where_clause.get(), schema, row)) {
                // Project row
                std::vector<Value> proj_row;
                for (int idx : proj_indices) {
                    proj_row.push_back(row[idx]);
                }
                res.rows.push_back(std::move(proj_row));
            }
        }
    }

    res.message = "Successfully returned " + std::to_string(res.rows.size()) + " rows.";
    return res;
}

QueryResult Executor::execute_insert(const InsertStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id) {
    QueryResult res;

    // Validate values against schema
    if (stmt.values.size() != schema.columns().size()) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Values count does not match schema column count");
    }

    std::vector<Value> row = stmt.values;

    // Type casting/validation
    for (size_t i = 0; i < schema.columns().size(); ++i) {
        if (row[i].is_null()) {
            if (schema.get_col(i).is_primary_key) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Primary key column cannot be null");
            }
            continue;
        }

        if (row[i].type() != schema.get_col(i).type) {
            // Check implicit numeric conversion
            if (schema.get_col(i).type == DataType::FLOAT && row[i].type() == DataType::INT) {
                row[i] = Value(static_cast<double>(row[i].as_int()));
            } else {
                throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value type mismatch for column " + schema.get_col(i).name);
            }
        }
    }

    // Check primary key uniqueness
    int pk_idx = -1;
    for (size_t i = 0; i < schema.columns().size(); ++i) {
        if (schema.get_col(i).is_primary_key) {
            pk_idx = static_cast<int>(i);
            break;
        }
    }

    if (pk_idx >= 0) {
        record_id_t existing_rid;
        if (index.find(row[pk_idx], existing_rid)) {
            throw DBException(StatusCode::ERR_DUPLICATE_KEY, "Duplicate primary key value: " + row[pk_idx].to_string());
        }
    }

    // Write row to table heap
    std::vector<uint8_t> rec_data = schema.serialize_row(row);
    record_id_t rid = table.insert_record(rec_data);

    // Append to WAL
    wal.append_record(txn_id, LogRecordType::UPDATE, get_page_id(rid), get_slot_id(rid), {}, rec_data);

    // Insert into Index
    if (pk_idx >= 0) {
        index.insert(row[pk_idx], rid);
    }

    res.message = "Successfully inserted 1 row (RID: " + std::to_string(rid) + ").";
    return res;
}

QueryResult Executor::execute_update(const UpdateStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id) {
    QueryResult res;

    int target_idx = schema.get_col_index(stmt.target_col);
    if (target_idx < 0) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Target column for SET not found: " + stmt.target_col);
    }

    const Column& target_col = schema.get_col(target_idx);
    if (target_col.is_primary_key) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Updating primary key is not supported");
    }

    // Check type of replacement
    Value updated_val = stmt.new_val;
    if (!updated_val.is_null() && updated_val.type() != target_col.type) {
        if (target_col.type == DataType::FLOAT && updated_val.type() == DataType::INT) {
            updated_val = Value(static_cast<double>(updated_val.as_int()));
        } else {
            throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value type mismatch for column " + target_col.name);
        }
    }

    // Look for matching rows
    std::vector<record_id_t> rids_to_update;
    Value lookup_key;
    bool is_index = check_index_scan(stmt.where_clause.get(), schema, lookup_key);

    if (is_index) {
        record_id_t rid;
        if (index.find(lookup_key, rid)) {
            rids_to_update.push_back(rid);
        }
    } else {
        record_id_t rid = table.get_first_record_id();
        while (get_page_id(rid) != INVALID_PAGE_ID) {
            rids_to_update.push_back(rid);
            rid = table.get_next_record_id(rid);
        }
    }

    int update_count = 0;
    for (record_id_t rid : rids_to_update) {
        std::vector<uint8_t> old_data;
        if (table.get_record(rid, old_data)) {
            std::vector<Value> row = schema.deserialize_row(old_data.data(), old_data.size());
            if (evaluate_expression(stmt.where_clause.get(), schema, row)) {
                // Update value
                row[target_idx] = updated_val;
                std::vector<uint8_t> new_data = schema.serialize_row(row);

                // Try in-place update first
                if (table.update_record(rid, new_data)) {
                    wal.append_record(txn_id, LogRecordType::UPDATE, get_page_id(rid), get_slot_id(rid), old_data, new_data);
                } else {
                    // Out-of-space on page: Delete and insert
                    table.delete_record(rid);
                    record_id_t new_rid = table.insert_record(new_data);

                    // If index is present, we must update the index mapping
                    int pk_idx = -1;
                    for (size_t i = 0; i < schema.columns().size(); ++i) {
                        if (schema.get_col(i).is_primary_key) {
                            pk_idx = static_cast<int>(i);
                            break;
                        }
                    }
                    if (pk_idx >= 0) {
                        index.insert(row[pk_idx], new_rid);
                    }

                    wal.append_record(txn_id, LogRecordType::UPDATE, get_page_id(new_rid), get_slot_id(new_rid), {}, new_data);
                }
                update_count++;
            }
        }
    }

    res.message = "Successfully updated " + std::to_string(update_count) + " rows.";
    return res;
}

QueryResult Executor::execute_delete(const DeleteStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id) {
    QueryResult res;

    // Find matching rows
    std::vector<record_id_t> rids_to_delete;
    Value lookup_key;
    bool is_index = check_index_scan(stmt.where_clause.get(), schema, lookup_key);

    if (is_index) {
        record_id_t rid;
        if (index.find(lookup_key, rid)) {
            rids_to_delete.push_back(rid);
        }
    } else {
        record_id_t rid = table.get_first_record_id();
        while (get_page_id(rid) != INVALID_PAGE_ID) {
            rids_to_delete.push_back(rid);
            rid = table.get_next_record_id(rid);
        }
    }

    int delete_count = 0;
    // Track index key positions
    int pk_idx = -1;
    for (size_t i = 0; i < schema.columns().size(); ++i) {
        if (schema.get_col(i).is_primary_key) {
            pk_idx = static_cast<int>(i);
            break;
        }
    }

    for (record_id_t rid : rids_to_delete) {
        std::vector<uint8_t> old_data;
        if (table.get_record(rid, old_data)) {
            std::vector<Value> row = schema.deserialize_row(old_data.data(), old_data.size());
            if (evaluate_expression(stmt.where_clause.get(), schema, row)) {
                // Perform delete
                table.delete_record(rid);

                // Append delete entry in WAL (updating target with empty indicates deletion)
                wal.append_record(txn_id, LogRecordType::UPDATE, get_page_id(rid), get_slot_id(rid), old_data, {});

                // Remove from index
                if (pk_idx >= 0) {
                    index.remove(row[pk_idx]);
                }
                delete_count++;
            }
        }
    }

    res.message = "Successfully deleted " + std::to_string(delete_count) + " rows.";
    return res;
}

// Volcano Executors Implementations

static bool is_version_visible(txn_id_t xmin, txn_id_t xmax, txn_id_t query_txn_id, const TransactionManager* txn_mgr) {
    if (query_txn_id == INVALID_TXN_ID || !txn_mgr) {
        return true;
    }
    
    bool xmin_visible = (xmin == query_txn_id) || txn_mgr->is_committed(xmin);
    if (!xmin_visible) {
        return false;
    }

    if (xmax == 0) {
        return true;
    }
    if (xmax == query_txn_id) {
        return false;
    }
    if (txn_mgr->is_committed(xmax)) {
        return false;
    }

    return true;
}

SeqScanExecutor::SeqScanExecutor(const TableHeap& table, const Schema& schema, const Expression* filter, txn_id_t txn_id, const TransactionManager* txn_mgr)
    : table_(table), schema_(schema), filter_(filter), txn_id_(txn_id), txn_mgr_(txn_mgr) {
    cursor_rid_ = make_record_id(INVALID_PAGE_ID, 0);
}

void SeqScanExecutor::init() {
    cursor_rid_ = table_.get_first_record_id();
}

bool SeqScanExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    while (get_page_id(cursor_rid_) != INVALID_PAGE_ID) {
        rid = cursor_rid_;
        cursor_rid_ = table_.get_next_record_id(cursor_rid_);

        std::vector<uint8_t> rec_data;
        if (table_.get_record(rid, rec_data)) {
            txn_id_t xmin = 0;
            txn_id_t xmax = 0;
            row = schema_.deserialize_row(rec_data.data(), rec_data.size(), &xmin, &xmax);
            if (is_version_visible(xmin, xmax, txn_id_, txn_mgr_)) {
                if (Executor::evaluate_expression(filter_, schema_, row)) {
                    return true;
                }
            }
        }
    }
    return false;
}

IndexScanExecutor::IndexScanExecutor(const TableHeap& table, const BTreeIndex& index, const Schema& schema, Value key, const Expression* filter, txn_id_t txn_id, const TransactionManager* txn_mgr)
    : table_(table), index_(index), schema_(schema), lookup_key_(std::move(key)), filter_(filter), txn_id_(txn_id), txn_mgr_(txn_mgr) {}

void IndexScanExecutor::init() {
    scanned_ = false;
}

bool IndexScanExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (scanned_) return false;

    if (index_.find(lookup_key_, rid)) {
        std::vector<uint8_t> rec_data;
        if (table_.get_record(rid, rec_data)) {
            txn_id_t xmin = 0;
            txn_id_t xmax = 0;
            row = schema_.deserialize_row(rec_data.data(), rec_data.size(), &xmin, &xmax);
            scanned_ = true;
            if (is_version_visible(xmin, xmax, txn_id_, txn_mgr_)) {
                if (Executor::evaluate_expression(filter_, schema_, row)) {
                    return true;
                }
            }
        }
    }

    scanned_ = true;
    return false;
}

InsertExecutor::InsertExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::vector<Value> values, txn_id_t txn_id)
    : table_(table), index_(index), wal_(wal), schema_(schema), values_(std::move(values)), txn_id_(txn_id) {}

void InsertExecutor::init() {
    executed_ = false;
}

bool InsertExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (executed_) return false;

    if (values_.size() < schema_.columns().size()) {
        for (size_t i = values_.size(); i < schema_.columns().size(); ++i) {
            if (schema_.columns()[i].has_default) {
                values_.push_back(schema_.columns()[i].default_val);
            } else if (schema_.columns()[i].is_not_null) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Missing value for NOT NULL column without default: " + schema_.columns()[i].name);
            } else {
                values_.push_back(Value());
            }
        }
    }

    if (values_.size() != schema_.columns().size()) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Insert values count mismatch");
    }

    // Type coerce / Validate
    for (size_t i = 0; i < schema_.columns().size(); ++i) {
        if (values_[i].is_null()) {
            if (schema_.columns()[i].is_primary_key) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Primary key cannot be null");
            }
            continue;
        }
        if (values_[i].type() != schema_.columns()[i].type) {
            if (schema_.columns()[i].type == DataType::FLOAT && values_[i].type() == DataType::INT) {
                values_[i] = Value(static_cast<double>(values_[i].as_int()));
            } else {
                throw DBException(StatusCode::ERR_TYPE_MISMATCH, "Value type mismatch");
            }
        }
    }

    // PK check
    int pk_idx = -1;
    for (size_t i = 0; i < schema_.columns().size(); ++i) {
        if (schema_.columns()[i].is_primary_key) {
            pk_idx = static_cast<int>(i);
            break;
        }
    }

    if (pk_idx >= 0) {
        record_id_t existing_rid;
        if (index_.find(values_[pk_idx], existing_rid)) {
            throw DBException(StatusCode::ERR_DUPLICATE_KEY, "Duplicate primary key: " + values_[pk_idx].to_string());
        }
    }

    // Unique constraints check
    for (size_t i = 0; i < schema_.columns().size(); ++i) {
        if (schema_.columns()[i].is_unique && !schema_.columns()[i].is_primary_key) {
            record_id_t scan_rid = table_.get_first_record_id();
            while (get_page_id(scan_rid) != INVALID_PAGE_ID) {
                std::vector<uint8_t> scan_data;
                if (table_.get_record(scan_rid, scan_data)) {
                    txn_id_t xmin = 0;
                    txn_id_t xmax = 0;
                    std::vector<Value> scan_row = schema_.deserialize_row(scan_data.data(), scan_data.size(), &xmin, &xmax);
                    if (xmax == 0 && scan_row[i] == values_[i]) {
                        throw DBException(StatusCode::ERR_DUPLICATE_KEY, "Duplicate unique key constraint violated for column: " + schema_.columns()[i].name);
                    }
                }
                scan_rid = table_.get_next_record_id(scan_rid);
            }
        }
    }

    std::vector<uint8_t> rec_data = schema_.serialize_row(values_, txn_id_, 0);
    rid = table_.insert_record(rec_data);

    // Index insert
    if (pk_idx >= 0) {
        index_.insert(values_[pk_idx], rid);
    }

    wal_.append_record(txn_id_, LogRecordType::UPDATE, get_page_id(rid), get_slot_id(rid), {}, rec_data);

    row = values_;
    executed_ = true;
    return true;
}

UpdateExecutor::UpdateExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::string target_col, Value new_val, std::shared_ptr<AbstractExecutor> child, txn_id_t txn_id)
    : table_(table), index_(index), wal_(wal), schema_(schema), target_col_(std::move(target_col)), new_val_(std::move(new_val)), child_(std::move(child)), txn_id_(txn_id) {}

void UpdateExecutor::init() {
    child_->init();
    executed_ = false;
}

bool UpdateExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    std::vector<Value> child_row;
    record_id_t child_rid;

    int target_idx = schema_.get_col_index(target_col_);
    if (target_idx < 0) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Target column not found: " + target_col_);
    }

    if (schema_.columns()[target_idx].is_primary_key) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Updating primary key is not supported");
    }

    if (child_->next(child_row, child_rid)) {
        // Unique constraints check for update
        if (schema_.columns()[target_idx].is_unique) {
            record_id_t scan_rid = table_.get_first_record_id();
            while (get_page_id(scan_rid) != INVALID_PAGE_ID) {
                if (scan_rid != child_rid) {
                    std::vector<uint8_t> scan_data;
                    if (table_.get_record(scan_rid, scan_data)) {
                        txn_id_t xmin = 0;
                        txn_id_t xmax = 0;
                        std::vector<Value> scan_row = schema_.deserialize_row(scan_data.data(), scan_data.size(), &xmin, &xmax);
                        if (xmax == 0 && scan_row[target_idx] == new_val_) {
                            throw DBException(StatusCode::ERR_DUPLICATE_KEY, "Duplicate unique key constraint violated for column: " + schema_.columns()[target_idx].name);
                        }
                    }
                }
                scan_rid = table_.get_next_record_id(scan_rid);
            }
        }

        std::vector<uint8_t> old_data;
        if (table_.get_record(child_rid, old_data)) {
            txn_id_t old_xmin = 0;
            txn_id_t old_xmax = 0;
            std::vector<Value> child_row_before = schema_.deserialize_row(old_data.data(), old_data.size(), &old_xmin, &old_xmax);

            // 1. Logically delete the old version by setting xmax = txn_id_
            std::vector<uint8_t> updated_old_data = schema_.serialize_row(child_row_before, old_xmin, txn_id_);
            table_.update_record(child_rid, updated_old_data);

            // 2. Insert the new version record
            child_row[target_idx] = new_val_;
            std::vector<uint8_t> new_data = schema_.serialize_row(child_row, txn_id_, 0);
            record_id_t new_rid = table_.insert_record(new_data);

            int pk_idx = -1;
            for (size_t i = 0; i < schema_.columns().size(); ++i) {
                if (schema_.columns()[i].is_primary_key) {
                    pk_idx = static_cast<int>(i);
                    break;
                }
            }
            if (pk_idx >= 0) {
                index_.insert(child_row[pk_idx], new_rid);
            }

            wal_.append_record(txn_id_, LogRecordType::UPDATE, get_page_id(child_rid), get_slot_id(child_rid), old_data, updated_old_data);
            wal_.append_record(txn_id_, LogRecordType::UPDATE, get_page_id(new_rid), get_slot_id(new_rid), {}, new_data);

            row = child_row;
            rid = new_rid;
            return true;
        }
    }

    return false;
}

DeleteExecutor::DeleteExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::shared_ptr<AbstractExecutor> child, txn_id_t txn_id)
    : table_(table), index_(index), wal_(wal), schema_(schema), child_(std::move(child)), txn_id_(txn_id) {}

void DeleteExecutor::init() {
    child_->init();
}

bool DeleteExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    std::vector<Value> child_row;
    record_id_t child_rid;

    int pk_idx = -1;
    for (size_t i = 0; i < schema_.columns().size(); ++i) {
        if (schema_.columns()[i].is_primary_key) {
            pk_idx = static_cast<int>(i);
            break;
        }
    }

    if (child_->next(child_row, child_rid)) {
        std::vector<uint8_t> old_data;
        if (table_.get_record(child_rid, old_data)) {
            txn_id_t old_xmin = 0;
            txn_id_t old_xmax = 0;
            std::vector<Value> row_values = schema_.deserialize_row(old_data.data(), old_data.size(), &old_xmin, &old_xmax);

            // Logically delete the record by setting xmax = txn_id_
            std::vector<uint8_t> updated_old_data = schema_.serialize_row(row_values, old_xmin, txn_id_);
            table_.update_record(child_rid, updated_old_data);

            wal_.append_record(txn_id_, LogRecordType::UPDATE, get_page_id(child_rid), get_slot_id(child_rid), old_data, updated_old_data);

            row = child_row;
            rid = child_rid;
            return true;
        }
    }

    return false;
}

NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::shared_ptr<AbstractExecutor> left, std::shared_ptr<AbstractExecutor> right, const Expression* join_cond, Schema out_schema)
    : left_(std::move(left)), right_(std::move(right)), join_cond_(join_cond), out_schema_(std::move(out_schema)) {}

void NestedLoopJoinExecutor::init() {
    left_->init();
    right_->init();
    left_row_.clear();
    left_rid_ = make_record_id(INVALID_PAGE_ID, 0);
}

bool NestedLoopJoinExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    while (true) {
        if (left_row_.empty()) {
            if (!left_->next(left_row_, left_rid_)) {
                return false;
            }
            right_->init();
        }

        std::vector<Value> right_row;
        record_id_t right_rid;
        if (right_->next(right_row, right_rid)) {
            std::vector<Value> joined = left_row_;
            joined.insert(joined.end(), right_row.begin(), right_row.end());

            if (Executor::evaluate_expression(join_cond_, out_schema_, joined)) {
                row = std::move(joined);
                rid = left_rid_;
                return true;
            }
        } else {
            left_row_.clear();
        }
    }
}

SortExecutor::SortExecutor(std::shared_ptr<AbstractExecutor> child, std::vector<std::string> sort_cols, std::vector<bool> order_asc)
    : child_(std::move(child)), sort_cols_(std::move(sort_cols)), order_asc_(std::move(order_asc)) {}

void SortExecutor::init() {
    child_->init();
    sorted_rows_.clear();
    cursor_ = 0;

    std::vector<Value> r;
    record_id_t rid;
    while (child_->next(r, rid)) {
        sorted_rows_.push_back({std::move(r), rid});
    }

    const Schema& schema = child_->get_schema();
    std::vector<int> col_indices;
    for (const auto& col : sort_cols_) {
        col_indices.push_back(schema.get_col_index(col));
    }

    std::sort(sorted_rows_.begin(), sorted_rows_.end(), [&](const auto& a, const auto& b) {
        for (size_t i = 0; i < col_indices.size(); ++i) {
            int idx = col_indices[i];
            if (idx < 0) continue;
            bool asc = order_asc_[i];
            if (a.first[idx] == b.first[idx]) continue;
            if (asc) {
                return a.first[idx] < b.first[idx];
            } else {
                return a.first[idx] > b.first[idx];
            }
        }
        return false;
    });
}

bool SortExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (cursor_ >= sorted_rows_.size()) {
        return false;
    }
    row = sorted_rows_[cursor_].first;
    rid = sorted_rows_[cursor_].second;
    cursor_++;
    return true;
}

LimitExecutor::LimitExecutor(std::shared_ptr<AbstractExecutor> child, size_t limit, size_t offset)
    : child_(std::move(child)), limit_(limit), offset_(offset) {}

void LimitExecutor::init() {
    child_->init();
    count_ = 0;
    skipped_ = 0;
}

bool LimitExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    while (skipped_ < offset_) {
        if (!child_->next(row, rid)) {
            return false;
        }
        skipped_++;
    }

    if (count_ >= limit_) {
        return false;
    }

    if (child_->next(row, rid)) {
        count_++;
        return true;
    }

    return false;
}

// HashJoinExecutor Implementation

HashJoinExecutor::HashJoinExecutor(std::shared_ptr<AbstractExecutor> left, std::shared_ptr<AbstractExecutor> right, std::string left_col, std::string right_col, Schema out_schema)
    : left_(std::move(left)), right_(std::move(right)), left_col_(std::move(left_col)), right_col_(std::move(right_col)), out_schema_(std::move(out_schema)) {}

void HashJoinExecutor::init() {
    left_->init();
    right_->init();
    hash_table_.clear();
    matched_rows_.clear();
    cursor_ = 0;

    int left_key_idx = left_->get_schema().get_col_index(left_col_);
    if (left_key_idx < 0) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Left join column not found: " + left_col_);
    }

    // Build Phase
    std::vector<Value> left_row;
    record_id_t left_rid;
    while (left_->next(left_row, left_rid)) {
        hash_table_.insert({left_row[left_key_idx].to_string(), std::move(left_row)});
    }

    int right_key_idx = right_->get_schema().get_col_index(right_col_);
    if (right_key_idx < 0) {
        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Right join column not found: " + right_col_);
    }

    // Probe Phase
    std::vector<Value> right_row;
    record_id_t right_rid;
    while (right_->next(right_row, right_rid)) {
        std::string join_key = right_row[right_key_idx].to_string();
        auto range = hash_table_.equal_range(join_key);
        for (auto it = range.first; it != range.second; ++it) {
            std::vector<Value> joined = it->second;
            joined.insert(joined.end(), right_row.begin(), right_row.end());
            matched_rows_.push_back(std::move(joined));
        }
    }
}

bool HashJoinExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (cursor_ >= matched_rows_.size()) {
        return false;
    }
    row = std::move(matched_rows_[cursor_++]);
    rid = make_record_id(INVALID_PAGE_ID, 0);
    return true;
}

void InstrumentedExecutor::init() {
    auto start = std::chrono::high_resolution_clock::now();
    child_->init();
    auto end = std::chrono::high_resolution_clock::now();
    init_duration_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
}

bool InstrumentedExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    metrics_.next_calls++;
    auto start = std::chrono::high_resolution_clock::now();
    bool res = child_->next(row, rid);
    auto end = std::chrono::high_resolution_clock::now();
    metrics_.total_duration_ms += std::chrono::duration<double, std::milli>(end - start).count();
    if (res) {
        metrics_.rows_produced++;
    }
    return res;
}

static std::string datatype_to_str(DataType type) {
    switch (type) {
        case DataType::INT: return "INT";
        case DataType::FLOAT: return "FLOAT";
        case DataType::VARCHAR: return "VARCHAR";
        case DataType::BLOB: return "BLOB";
        default: return "UNKNOWN";
    }
}

void SystemScanExecutor::init() {
    rows_.clear();
    cursor_ = 0;

    std::vector<std::string> table_names = catalog_.get_table_names();

    if (system_table_name_ == "system_tables") {
        for (const auto& tbl_name : table_names) {
            TableMetadata* tbl = catalog_.get_table(tbl_name);
            if (tbl) {
                std::vector<Value> row;
                row.push_back(Value(tbl->name));
                row.push_back(Value(static_cast<int64_t>(tbl->stats.num_rows)));
                row.push_back(Value(static_cast<int64_t>(tbl->stats.avg_row_size)));
                rows_.push_back(std::move(row));
            }
        }
    } else if (system_table_name_ == "system_columns") {
        for (const auto& tbl_name : table_names) {
            TableMetadata* tbl = catalog_.get_table(tbl_name);
            if (tbl) {
                for (const auto& col : tbl->schema.columns()) {
                    std::vector<Value> row;
                    row.push_back(Value(tbl->name));
                    row.push_back(Value(col.name));
                    row.push_back(Value(datatype_to_str(col.type)));
                    row.push_back(Value(static_cast<int64_t>(col.is_not_null ? 0 : 1)));
                    row.push_back(Value(static_cast<int64_t>(col.is_primary_key ? 1 : 0)));
                    rows_.push_back(std::move(row));
                }
            }
        }
    } else if (system_table_name_ == "system_indexes") {
        for (const auto& tbl_name : table_names) {
            std::vector<IndexMetadata*> idxs = catalog_.get_table_indexes(tbl_name);
            TableMetadata* tbl = catalog_.get_table(tbl_name);
            if (tbl) {
                for (auto* idx_meta : idxs) {
                    std::vector<Value> row;
                    row.push_back(Value(idx_meta->name));
                    row.push_back(Value(idx_meta->table_name));
                    int attr_idx = idx_meta->key_attrs[0];
                    row.push_back(Value(tbl->schema.get_col(attr_idx).name));
                    rows_.push_back(std::move(row));
                }
            }
        }
    }
}

bool SystemScanExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (cursor_ >= rows_.size()) {
        return false;
    }
    row = rows_[cursor_++];
    rid = make_record_id(INVALID_PAGE_ID, 0);
    return true;
}

AggregationExecutor::AggregationExecutor(std::shared_ptr<AbstractExecutor> child, std::vector<std::shared_ptr<Expression>> aggregates, std::vector<AggregationType> types, Schema schema)
    : child_(std::move(child)), aggregates_(std::move(aggregates)), types_(std::move(types)), schema_(std::move(schema)) {}

void AggregationExecutor::init() {
    child_->init();
    executed_ = false;
    aggregated_row_.clear();

    if (aggregates_.empty()) {
        return;
    }

    std::vector<std::vector<Value>> child_rows;
    std::vector<Value> child_row;
    record_id_t child_rid;
    while (child_->next(child_row, child_rid)) {
        child_rows.push_back(std::move(child_row));
    }

    const Schema& child_schema = child_->get_schema();

    struct Accumulator {
        int64_t count{0};
        double sum_float{0.0};
        int64_t sum_int{0};
        Value min_val;
        Value max_val;
        bool has_any{false};
    };

    std::vector<Accumulator> accs(aggregates_.size());

    for (const auto& row : child_rows) {
        for (size_t i = 0; i < aggregates_.size(); ++i) {
            const auto& expr = aggregates_[i];
            AggregationType type = types_[i];
            
            Value val;
            if (expr->type == ExpressionType::LITERAL) {
                val = Value(1LL);
            } else {
                int col_idx = child_schema.get_col_index(expr->col_name);
                if (col_idx >= 0) {
                    val = row[col_idx];
                }
            }

            if (val.is_null()) {
                if (type == AggregationType::COUNT && expr->type == ExpressionType::LITERAL) {
                    accs[i].count++;
                    accs[i].has_any = true;
                }
                continue;
            }

            accs[i].has_any = true;
            if (type == AggregationType::COUNT) {
                accs[i].count++;
            } else if (type == AggregationType::SUM || type == AggregationType::AVG) {
                if (val.type() == DataType::INT) {
                    accs[i].sum_int += val.as_int();
                } else if (val.type() == DataType::FLOAT) {
                    accs[i].sum_float += val.as_float();
                }
                accs[i].count++;
            } else if (type == AggregationType::MIN) {
                if (accs[i].min_val.is_null()) {
                    accs[i].min_val = val;
                } else {
                    if (val.type() == DataType::INT && val.as_int() < accs[i].min_val.as_int()) {
                        accs[i].min_val = val;
                    } else if (val.type() == DataType::FLOAT && val.as_float() < accs[i].min_val.as_float()) {
                        accs[i].min_val = val;
                    } else if (val.type() == DataType::VARCHAR && val.as_string() < accs[i].min_val.as_string()) {
                        accs[i].min_val = val;
                    }
                }
            } else if (type == AggregationType::MAX) {
                if (accs[i].max_val.is_null()) {
                    accs[i].max_val = val;
                } else {
                    if (val.type() == DataType::INT && val.as_int() > accs[i].max_val.as_int()) {
                        accs[i].max_val = val;
                    } else if (val.type() == DataType::FLOAT && val.as_float() > accs[i].max_val.as_float()) {
                        accs[i].max_val = val;
                    } else if (val.type() == DataType::VARCHAR && val.as_string() > accs[i].max_val.as_string()) {
                        accs[i].max_val = val;
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < aggregates_.size(); ++i) {
        AggregationType type = types_[i];
        const auto& col = schema_.get_col(i);

        if (type == AggregationType::COUNT) {
            aggregated_row_.push_back(Value(accs[i].count));
        } else if (type == AggregationType::SUM) {
            if (!accs[i].has_any) {
                aggregated_row_.push_back(Value());
            } else if (col.type == DataType::INT) {
                aggregated_row_.push_back(Value(accs[i].sum_int));
            } else {
                aggregated_row_.push_back(Value(accs[i].sum_float));
            }
        } else if (type == AggregationType::AVG) {
            if (!accs[i].has_any || accs[i].count == 0) {
                aggregated_row_.push_back(Value());
            } else {
                double total = static_cast<double>(accs[i].sum_int) + accs[i].sum_float;
                aggregated_row_.push_back(Value(total / accs[i].count));
            }
        } else if (type == AggregationType::MIN) {
            aggregated_row_.push_back(accs[i].min_val);
        } else if (type == AggregationType::MAX) {
            aggregated_row_.push_back(accs[i].max_val);
        }
    }
}

bool AggregationExecutor::next(std::vector<Value>& row, record_id_t& rid) {
    if (executed_) {
        return false;
    }
    row = aggregated_row_;
    rid = make_record_id(INVALID_PAGE_ID, 0);
    executed_ = true;
    return true;
}

} // namespace luminadb
