#ifndef LUMINADB_QUERY_PLANNER_H
#define LUMINADB_QUERY_PLANNER_H

#include <memory>
#include <vector>
#include <string>
#include "luminadb/query/ast.h"
#include "luminadb/core/schema.h"
#include "luminadb/core/catalog.h"

namespace luminadb {

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    INSERT,
    UPDATE,
    DELETE_PLAN,
    NESTED_LOOP_JOIN,
    HASH_JOIN,
    AGGREGATION,
    SORT,
    LIMIT,
    SYSTEM_SCAN
};

class AbstractPlanNode {
public:
    explicit AbstractPlanNode(PlanType type, std::shared_ptr<Schema> output_schema, std::vector<std::shared_ptr<AbstractPlanNode>> children = {})
        : type_(type), output_schema_(std::move(output_schema)), children_(std::move(children)) {}
    
    virtual ~AbstractPlanNode() = default;

    PlanType get_type() const { return type_; }
    const Schema& get_output_schema() const { return *output_schema_; }
    std::shared_ptr<Schema> get_output_schema_ptr() const { return output_schema_; }
    
    const std::vector<std::shared_ptr<AbstractPlanNode>>& get_children() const { return children_; }
    const std::shared_ptr<AbstractPlanNode> get_child_at(size_t idx) const { return children_[idx]; }

    virtual std::string to_string(int indent = 0) const = 0;

private:
    PlanType type_;
    std::shared_ptr<Schema> output_schema_;
    std::vector<std::shared_ptr<AbstractPlanNode>> children_;
};

// Plan node implementations
class SeqScanPlanNode : public AbstractPlanNode {
public:
    SeqScanPlanNode(std::shared_ptr<Schema> schema, std::string table_name, std::shared_ptr<Expression> filter)
        : AbstractPlanNode(PlanType::SEQ_SCAN, std::move(schema)), table_name_(std::move(table_name)), filter_(std::move(filter)) {}

    const std::string& get_table_name() const { return table_name_; }
    const Expression* get_filter() const { return filter_.get(); }

    std::string to_string(int indent = 0) const override;

private:
    std::string table_name_;
    std::shared_ptr<Expression> filter_;
};

class IndexScanPlanNode : public AbstractPlanNode {
public:
    IndexScanPlanNode(std::shared_ptr<Schema> schema, std::string table_name, Value key, std::shared_ptr<Expression> filter)
        : AbstractPlanNode(PlanType::INDEX_SCAN, std::move(schema)), table_name_(std::move(table_name)), lookup_key_(std::move(key)), filter_(std::move(filter)) {}

    const std::string& get_table_name() const { return table_name_; }
    const Value& get_lookup_key() const { return lookup_key_; }
    const Expression* get_filter() const { return filter_.get(); }

    std::string to_string(int indent = 0) const override;

private:
    std::string table_name_;
    Value lookup_key_;
    std::shared_ptr<Expression> filter_;
};

class InsertPlanNode : public AbstractPlanNode {
public:
    InsertPlanNode(std::string table_name, std::vector<Value> values)
        : AbstractPlanNode(PlanType::INSERT, nullptr), table_name_(std::move(table_name)), values_(std::move(values)) {}

    const std::string& get_table_name() const { return table_name_; }
    const std::vector<Value>& get_values() const { return values_; }

    std::string to_string(int indent = 0) const override;

private:
    std::string table_name_;
    std::vector<Value> values_;
};

class UpdatePlanNode : public AbstractPlanNode {
public:
    UpdatePlanNode(std::string table_name, std::string target_col, Value new_val, std::shared_ptr<Expression> filter)
        : AbstractPlanNode(PlanType::UPDATE, nullptr), table_name_(std::move(table_name)), target_col_(std::move(target_col)), new_val_(std::move(new_val)), filter_(std::move(filter)) {}

    const std::string& get_table_name() const { return table_name_; }
    const std::string& get_target_col() const { return target_col_; }
    const Value& get_new_val() const { return new_val_; }
    const Expression* get_filter() const { return filter_.get(); }

    std::string to_string(int indent = 0) const override;

private:
    std::string table_name_;
    std::string target_col_;
    Value new_val_;
    std::shared_ptr<Expression> filter_;
};

class DeletePlanNode : public AbstractPlanNode {
public:
    DeletePlanNode(std::string table_name, std::shared_ptr<Expression> filter)
        : AbstractPlanNode(PlanType::DELETE_PLAN, nullptr), table_name_(std::move(table_name)), filter_(std::move(filter)) {}

    const std::string& get_table_name() const { return table_name_; }
    const Expression* get_filter() const { return filter_.get(); }

    std::string to_string(int indent = 0) const override;

private:
    std::string table_name_;
    std::shared_ptr<Expression> filter_;
};

enum class JoinType {
    INNER,
    LEFT,
    OUTER
};

class NestedLoopJoinPlanNode : public AbstractPlanNode {
public:
    NestedLoopJoinPlanNode(std::shared_ptr<Schema> schema, std::shared_ptr<AbstractPlanNode> left, std::shared_ptr<AbstractPlanNode> right, std::shared_ptr<Expression> join_cond)
        : AbstractPlanNode(PlanType::NESTED_LOOP_JOIN, std::move(schema), {left, right}), join_cond_(std::move(join_cond)) {}

    const Expression* get_join_cond() const { return join_cond_.get(); }

    std::string to_string(int indent = 0) const override;

private:
    std::shared_ptr<Expression> join_cond_;
};

class HashJoinPlanNode : public AbstractPlanNode {
public:
    HashJoinPlanNode(std::shared_ptr<Schema> schema, std::shared_ptr<AbstractPlanNode> left, std::shared_ptr<AbstractPlanNode> right, std::string left_col, std::string right_col)
        : AbstractPlanNode(PlanType::HASH_JOIN, std::move(schema), {left, right}), left_col_(std::move(left_col)), right_col_(std::move(right_col)) {}

    const std::string& get_left_col() const { return left_col_; }
    const std::string& get_right_col() const { return right_col_; }

    std::string to_string(int indent = 0) const override;

private:
    std::string left_col_;
    std::string right_col_;
};



class AggregationPlanNode : public AbstractPlanNode {
public:
    AggregationPlanNode(std::shared_ptr<Schema> schema, std::shared_ptr<AbstractPlanNode> child, std::vector<std::shared_ptr<Expression>> aggregates, std::vector<AggregationType> types)
        : AbstractPlanNode(PlanType::AGGREGATION, std::move(schema), {child}), aggregates_(std::move(aggregates)), types_(std::move(types)) {}

    const std::vector<std::shared_ptr<Expression>>& get_aggregates() const { return aggregates_; }
    const std::vector<AggregationType>& get_types() const { return types_; }

    std::string to_string(int indent = 0) const override;

private:
    std::vector<std::shared_ptr<Expression>> aggregates_;
    std::vector<AggregationType> types_;
};

class SortPlanNode : public AbstractPlanNode {
public:
    SortPlanNode(std::shared_ptr<Schema> schema, std::shared_ptr<AbstractPlanNode> child, std::vector<std::string> sort_cols, std::vector<bool> order_asc)
        : AbstractPlanNode(PlanType::SORT, std::move(schema), {child}), sort_cols_(std::move(sort_cols)), order_asc_(std::move(order_asc)) {}

    const std::vector<std::string>& get_sort_cols() const { return sort_cols_; }
    const std::vector<bool>& get_order_asc() const { return order_asc_; }

    std::string to_string(int indent = 0) const override;

private:
    std::vector<std::string> sort_cols_;
    std::vector<bool> order_asc_;
};

class LimitPlanNode : public AbstractPlanNode {
public:
    LimitPlanNode(std::shared_ptr<Schema> schema, std::shared_ptr<AbstractPlanNode> child, size_t limit, size_t offset)
        : AbstractPlanNode(PlanType::LIMIT, std::move(schema), {child}), limit_(limit), offset_(offset) {}

    size_t get_limit() const { return limit_; }
    size_t get_offset() const { return offset_; }

    std::string to_string(int indent = 0) const override;

private:
    size_t limit_;
    size_t offset_;
};

class SystemScanPlanNode : public AbstractPlanNode {
public:
    SystemScanPlanNode(std::shared_ptr<Schema> schema, std::string system_table_name)
        : AbstractPlanNode(PlanType::SYSTEM_SCAN, std::move(schema)), system_table_name_(std::move(system_table_name)) {}

    const std::string& get_system_table_name() const { return system_table_name_; }

    std::string to_string(int indent = 0) const override;

private:
    std::string system_table_name_;
};

class Planner {
public:
    explicit Planner(const Catalog& catalog) : catalog_(catalog) {}

    std::shared_ptr<AbstractPlanNode> plan(const Statement& stmt);

private:
    const Catalog& catalog_;
};

} // namespace luminadb

#endif // LUMINADB_QUERY_PLANNER_H
