#ifndef LUMINADB_QUERY_EXECUTOR_H
#define LUMINADB_QUERY_EXECUTOR_H

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include "luminadb/core/schema.h"
#include "luminadb/storage/table_heap.h"
#include "luminadb/storage/btree_index.h"
#include "luminadb/storage/wal.h"
#include "luminadb/query/ast.h"

namespace luminadb {

class TransactionManager;

struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<DataType> column_types;
    std::vector<std::vector<Value>> rows;
    std::string message;
};

class AbstractExecutor {
public:
    virtual void init() = 0;
    virtual bool next(std::vector<Value>& row, record_id_t& rid) = 0;
    virtual const Schema& get_schema() const = 0;
    virtual ~AbstractExecutor() = default;
};

class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(const TableHeap& table, const Schema& schema, const Expression* filter, txn_id_t txn_id = INVALID_TXN_ID, const TransactionManager* txn_mgr = nullptr);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    const TableHeap& table_;
    Schema schema_;
    const Expression* filter_;
    txn_id_t txn_id_;
    const TransactionManager* txn_mgr_;
    record_id_t cursor_rid_;
};

class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(const TableHeap& table, const BTreeIndex& index, const Schema& schema, Value key, const Expression* filter, txn_id_t txn_id = INVALID_TXN_ID, const TransactionManager* txn_mgr = nullptr);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    const TableHeap& table_;
    const BTreeIndex& index_;
    Schema schema_;
    Value lookup_key_;
    const Expression* filter_;
    txn_id_t txn_id_;
    const TransactionManager* txn_mgr_;
    bool scanned_{false};
};

class Catalog;

class SystemScanExecutor : public AbstractExecutor {
public:
    SystemScanExecutor(const Catalog& catalog, Schema schema, std::string system_table_name)
        : catalog_(catalog), schema_(std::move(schema)), system_table_name_(std::move(system_table_name)) {}

    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    const Catalog& catalog_;
    Schema schema_;
    std::string system_table_name_;
    std::vector<std::vector<Value>> rows_;
    size_t cursor_{0};
};

class InsertExecutor : public AbstractExecutor {
public:
    InsertExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::vector<Value> values, txn_id_t txn_id);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    TableHeap& table_;
    BTreeIndex& index_;
    WALManager& wal_;
    Schema schema_;
    std::vector<Value> values_;
    txn_id_t txn_id_;
    bool executed_{false};
};

class UpdateExecutor : public AbstractExecutor {
public:
    UpdateExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::string target_col, Value new_val, std::shared_ptr<AbstractExecutor> child, txn_id_t txn_id);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    TableHeap& table_;
    BTreeIndex& index_;
    WALManager& wal_;
    Schema schema_;
    std::string target_col_;
    Value new_val_;
    std::shared_ptr<AbstractExecutor> child_;
    txn_id_t txn_id_;
    bool executed_{false};
};

class DeleteExecutor : public AbstractExecutor {
public:
    DeleteExecutor(TableHeap& table, BTreeIndex& index, WALManager& wal, const Schema& schema, std::shared_ptr<AbstractExecutor> child, txn_id_t txn_id);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    TableHeap& table_;
    BTreeIndex& index_;
    WALManager& wal_;
    Schema schema_;
    std::shared_ptr<AbstractExecutor> child_;
    txn_id_t txn_id_;
    bool executed_{false};
};

class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(std::shared_ptr<AbstractExecutor> left, std::shared_ptr<AbstractExecutor> right, const Expression* join_cond, Schema out_schema);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return out_schema_; }

private:
    std::shared_ptr<AbstractExecutor> left_;
    std::shared_ptr<AbstractExecutor> right_;
    const Expression* join_cond_;
    Schema out_schema_;
    std::vector<Value> left_row_;
    record_id_t left_rid_;
};

class HashJoinExecutor : public AbstractExecutor {
public:
    HashJoinExecutor(std::shared_ptr<AbstractExecutor> left, std::shared_ptr<AbstractExecutor> right, std::string left_col, std::string right_col, Schema out_schema);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return out_schema_; }

private:
    std::shared_ptr<AbstractExecutor> left_;
    std::shared_ptr<AbstractExecutor> right_;
    std::string left_col_;
    std::string right_col_;
    Schema out_schema_;
    std::unordered_multimap<std::string, std::vector<Value>> hash_table_;
    std::vector<std::vector<Value>> matched_rows_;
    size_t cursor_{0};
};

class SortExecutor : public AbstractExecutor {
public:
    SortExecutor(std::shared_ptr<AbstractExecutor> child, std::vector<std::string> sort_cols, std::vector<bool> order_asc);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return child_->get_schema(); }

private:
    std::shared_ptr<AbstractExecutor> child_;
    std::vector<std::string> sort_cols_;
    std::vector<bool> order_asc_;
    std::vector<std::pair<std::vector<Value>, record_id_t>> sorted_rows_;
    size_t cursor_{0};
};

class AggregationExecutor : public AbstractExecutor {
public:
    AggregationExecutor(std::shared_ptr<AbstractExecutor> child, std::vector<std::shared_ptr<Expression>> aggregates, std::vector<AggregationType> types, Schema schema);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return schema_; }

private:
    std::shared_ptr<AbstractExecutor> child_;
    std::vector<std::shared_ptr<Expression>> aggregates_;
    std::vector<AggregationType> types_;
    Schema schema_;
    std::vector<Value> aggregated_row_;
    bool executed_{false};
};

class LimitExecutor : public AbstractExecutor {
public:
    LimitExecutor(std::shared_ptr<AbstractExecutor> child, size_t limit, size_t offset);
    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return child_->get_schema(); }

private:
    std::shared_ptr<AbstractExecutor> child_;
    size_t limit_;
    size_t offset_;
    size_t count_{0};
    size_t skipped_{0};
};

class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(std::shared_ptr<AbstractExecutor> child, Schema out_schema, std::vector<int> proj_indices)
        : child_(std::move(child)), out_schema_(std::move(out_schema)), proj_indices_(std::move(proj_indices)) {}

    void init() override { child_->init(); }

    bool next(std::vector<Value>& row, record_id_t& rid) override {
        std::vector<Value> child_row;
        if (child_->next(child_row, rid)) {
            row.clear();
            for (int idx : proj_indices_) {
                row.push_back(child_row[idx]);
            }
            return true;
        }
        return false;
    }

    const Schema& get_schema() const override { return out_schema_; }

private:
    std::shared_ptr<AbstractExecutor> child_;
    Schema out_schema_;
    std::vector<int> proj_indices_;
};

struct ExecutorMetrics {
    int64_t next_calls{0};
    int64_t rows_produced{0};
    double total_duration_ms{0.0};
};

class InstrumentedExecutor : public AbstractExecutor {
public:
    explicit InstrumentedExecutor(std::shared_ptr<AbstractExecutor> child) : child_(std::move(child)) {}

    void init() override;
    bool next(std::vector<Value>& row, record_id_t& rid) override;
    const Schema& get_schema() const override { return child_->get_schema(); }

    ExecutorMetrics get_metrics() const { return metrics_; }
    double get_init_duration() const { return init_duration_ms_; }
    std::shared_ptr<AbstractExecutor> get_child() const { return child_; }

private:
    std::shared_ptr<AbstractExecutor> child_;
    ExecutorMetrics metrics_;
    double init_duration_ms_{0.0};
};

class Executor {
public:
    static QueryResult execute_select(const SelectStatement& stmt, 
                                      const Schema& schema, 
                                      const TableHeap& table, 
                                      const BTreeIndex& index);

    static QueryResult execute_insert(const InsertStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id);

    static QueryResult execute_update(const UpdateStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id);

    static QueryResult execute_delete(const DeleteStatement& stmt, 
                                      const Schema& schema, 
                                      TableHeap& table, 
                                      BTreeIndex& index,
                                      WALManager& wal,
                                      txn_id_t txn_id);

    static bool evaluate_expression(const Expression* expr, const Schema& schema, const std::vector<Value>& row);
};

} // namespace luminadb

#endif // LUMINADB_QUERY_EXECUTOR_H
