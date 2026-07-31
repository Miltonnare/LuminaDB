#include "luminadb/query/planner.h"
#include "luminadb/core/catalog.h"
#include <algorithm>

namespace luminadb {

// Helper to check if filter matches primary key index lookup
static bool check_index_match(const Expression* expr, const Schema& schema, Value& out_key) {
    if (!expr) return false;
    if (expr->type != ExpressionType::COMPARE_EQ) return false;

    if (expr->left && expr->left->type == ExpressionType::COL_REF &&
        expr->right && expr->right->type == ExpressionType::LITERAL) {
        int idx = schema.get_col_index(expr->left->col_name);
        if (idx >= 0 && schema.get_col(idx).is_primary_key) {
            out_key = expr->right->literal_val;
            return true;
        }
    }

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

std::shared_ptr<AbstractPlanNode> Planner::plan(const Statement& stmt) {
    switch (stmt.type()) {
        case StatementType::SELECT: {
            const auto& select_stmt = static_cast<const SelectStatement&>(stmt);

            std::string t_name = select_stmt.table_name;
            std::transform(t_name.begin(), t_name.end(), t_name.begin(), ::tolower);

            if (t_name == "system_tables" || t_name == "system_columns" || t_name == "system_indexes") {
                std::vector<Column> cols;
                if (t_name == "system_tables") {
                    cols.push_back(Column{"table_name", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"num_rows", DataType::INT, false, true, false, false, Value()});
                    cols.push_back(Column{"avg_row_size", DataType::INT, false, true, false, false, Value()});
                } else if (t_name == "system_columns") {
                    cols.push_back(Column{"table_name", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"column_name", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"column_type", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"is_nullable", DataType::INT, false, true, false, false, Value()});
                    cols.push_back(Column{"is_primary_key", DataType::INT, false, true, false, false, Value()});
                } else if (t_name == "system_indexes") {
                    cols.push_back(Column{"index_name", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"table_name", DataType::VARCHAR, false, true, false, false, Value()});
                    cols.push_back(Column{"column_name", DataType::VARCHAR, false, true, false, false, Value()});
                }
                auto sys_schema = std::make_shared<Schema>(t_name, std::move(cols));
                return std::make_shared<SystemScanPlanNode>(std::move(sys_schema), t_name);
            }

            TableMetadata* table = catalog_.get_table(select_stmt.table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + select_stmt.table_name);
            }

            if (select_stmt.has_join) {
                TableMetadata* right_table = catalog_.get_table(select_stmt.join_table);
                if (!right_table) {
                    throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Join table not found: " + select_stmt.join_table);
                }

                auto left_plan = std::make_shared<SeqScanPlanNode>(std::make_shared<Schema>(table->schema), select_stmt.table_name, select_stmt.where_clause);
                auto right_plan = std::make_shared<SeqScanPlanNode>(std::make_shared<Schema>(right_table->schema), select_stmt.join_table, nullptr);

                std::vector<Column> joined_cols = table->schema.columns();
                for (const auto& col : right_table->schema.columns()) {
                    joined_cols.push_back(col);
                }
                auto join_schema = std::make_shared<Schema>(select_stmt.table_name + "_" + select_stmt.join_table + "_joined", std::move(joined_cols));

                auto left_col_expr = std::make_shared<Expression>(Expression{ExpressionType::COL_REF, Value(), select_stmt.join_left_col, nullptr, nullptr});
                auto right_col_expr = std::make_shared<Expression>(Expression{ExpressionType::COL_REF, Value(), select_stmt.join_right_col, nullptr, nullptr});
                auto join_cond = std::make_shared<Expression>(Expression{ExpressionType::COMPARE_EQ, Value(), "", left_col_expr, right_col_expr});

                return std::make_shared<NestedLoopJoinPlanNode>(std::move(join_schema), left_plan, right_plan, join_cond);
            }

            if (select_stmt.is_aggregate) {
                auto child_scan = std::make_shared<SeqScanPlanNode>(std::make_shared<Schema>(table->schema), select_stmt.table_name, select_stmt.where_clause);

                std::vector<Column> agg_cols;
                std::vector<std::shared_ptr<Expression>> aggregates;

                for (size_t i = 0; i < select_stmt.agg_fields.size(); ++i) {
                    const auto& col_name = select_stmt.agg_fields[i];
                    AggregationType atype = select_stmt.agg_types[i];

                    DataType dtype = DataType::INT;
                    if (col_name == "*") {
                        dtype = DataType::INT;
                        aggregates.push_back(std::make_shared<Expression>(Expression{ExpressionType::LITERAL, Value(0LL), "", nullptr, nullptr}));
                    } else {
                        int c_idx = table->schema.get_col_index(col_name);
                        if (c_idx < 0) {
                            throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Column not found: " + col_name);
                        }
                        DataType base_type = table->schema.get_col(c_idx).type;
                        if (atype == AggregationType::COUNT) {
                            dtype = DataType::INT;
                        } else if (atype == AggregationType::AVG) {
                            dtype = DataType::FLOAT;
                        } else {
                            dtype = base_type;
                        }
                        aggregates.push_back(std::make_shared<Expression>(Expression{ExpressionType::COL_REF, Value(), col_name, nullptr, nullptr}));
                    }

                    std::string f_name = select_stmt.fields[i];
                    agg_cols.push_back(Column{f_name, dtype, false, true, false, false, Value()});
                }

                auto agg_schema = std::make_shared<Schema>(select_stmt.table_name + "_aggregate", std::move(agg_cols));
                return std::make_shared<AggregationPlanNode>(std::move(agg_schema), child_scan, std::move(aggregates), select_stmt.agg_types);
            }

            // Create target output schema based on fields
            std::shared_ptr<Schema> output_schema;
            if (select_stmt.fields.empty()) {
                output_schema = std::make_shared<Schema>(table->schema);
            } else {
                std::vector<Column> cols;
                for (const auto& col_name : select_stmt.fields) {
                    int idx = table->schema.get_col_index(col_name);
                    if (idx < 0) {
                        throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Column not found: " + col_name);
                    }
                    cols.push_back(table->schema.get_col(idx));
                }
                output_schema = std::make_shared<Schema>(select_stmt.table_name + "_projection", std::move(cols));
            }

            // Check if we can optimize with Index Scan
            Value lookup_key;
            bool is_index = check_index_match(select_stmt.where_clause.get(), table->schema, lookup_key);

            if (is_index) {
                return std::make_shared<IndexScanPlanNode>(std::move(output_schema), select_stmt.table_name, std::move(lookup_key), select_stmt.where_clause);
            } else {
                return std::make_shared<SeqScanPlanNode>(std::move(output_schema), select_stmt.table_name, select_stmt.where_clause);
            }
        }
        case StatementType::INSERT: {
            const auto& insert_stmt = static_cast<const InsertStatement&>(stmt);
            TableMetadata* table = catalog_.get_table(insert_stmt.table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + insert_stmt.table_name);
            }
            return std::make_shared<InsertPlanNode>(insert_stmt.table_name, insert_stmt.values);
        }
        case StatementType::UPDATE: {
            const auto& update_stmt = static_cast<const UpdateStatement&>(stmt);
            TableMetadata* table = catalog_.get_table(update_stmt.table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + update_stmt.table_name);
            }
            return std::make_shared<UpdatePlanNode>(update_stmt.table_name, update_stmt.target_col, update_stmt.new_val, update_stmt.where_clause);
        }
        case StatementType::DELETE: {
            const auto& delete_stmt = static_cast<const DeleteStatement&>(stmt);
            TableMetadata* table = catalog_.get_table(delete_stmt.table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + delete_stmt.table_name);
            }
            return std::make_shared<DeletePlanNode>(delete_stmt.table_name, delete_stmt.where_clause);
        }
        default:
            throw DBException(StatusCode::ERR_INTERNAL, "Planner: Unsupported statement type for physical planning");
    }
}

static std::string indent_str(int indent) {
    return std::string(indent * 2, ' ');
}

std::string SeqScanPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "SeqScan(" + table_name_ + ")";
    if (filter_) {
        res += " [Filter]";
    }
    return res;
}

std::string IndexScanPlanNode::to_string(int indent) const {
    return indent_str(indent) + "IndexScan(" + table_name_ + ", key=" + lookup_key_.to_string() + ")";
}

std::string InsertPlanNode::to_string(int indent) const {
    return indent_str(indent) + "Insert(" + table_name_ + ")";
}

std::string UpdatePlanNode::to_string(int indent) const {
    return indent_str(indent) + "Update(" + table_name_ + ", col=" + target_col_ + ", val=" + new_val_.to_string() + ")";
}

std::string DeletePlanNode::to_string(int indent) const {
    return indent_str(indent) + "Delete(" + table_name_ + ")";
}

std::string NestedLoopJoinPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "NestedLoopJoin\n";
    res += get_child_at(0)->to_string(indent + 1) + "\n";
    res += get_child_at(1)->to_string(indent + 1);
    return res;
}

std::string HashJoinPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "HashJoin(" + left_col_ + " = " + right_col_ + ")\n";
    res += get_child_at(0)->to_string(indent + 1) + "\n";
    res += get_child_at(1)->to_string(indent + 1);
    return res;
}

std::string AggregationPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "Aggregation\n";
    res += get_child_at(0)->to_string(indent + 1);
    return res;
}

std::string SortPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "Sort\n";
    res += get_child_at(0)->to_string(indent + 1);
    return res;
}

std::string LimitPlanNode::to_string(int indent) const {
    std::string res = indent_str(indent) + "Limit(limit=" + std::to_string(limit_) + ", offset=" + std::to_string(offset_) + ")\n";
    res += get_child_at(0)->to_string(indent + 1);
    return res;
}

std::string SystemScanPlanNode::to_string(int indent) const {
    return indent_str(indent) + "SystemScan(" + system_table_name_ + ")";
}

} // namespace luminadb
