#include "luminadb/query/optimizer.h"

namespace luminadb {

std::shared_ptr<AbstractPlanNode> Optimizer::optimize(std::shared_ptr<AbstractPlanNode> plan) {
    if (!plan) return nullptr;

    // Apply optimizations in sequence
    auto p1 = optimize_filter_pushdown(plan);
    auto p2 = optimize_index_scan(p1);
    auto p3 = optimize_join_selection(p2);
    return optimize_projection_pruning(p3);
}

std::shared_ptr<AbstractPlanNode> Optimizer::optimize_filter_pushdown(std::shared_ptr<AbstractPlanNode> plan) {
    if (!plan) return nullptr;

    // Recursively optimize children first
    std::vector<std::shared_ptr<AbstractPlanNode>> optimized_children;
    for (const auto& child : plan->get_children()) {
        optimized_children.push_back(optimize_filter_pushdown(child));
    }

    // Reconstruct current node with optimized children
    switch (plan->get_type()) {
        case PlanType::SEQ_SCAN: {
            auto scan = std::static_pointer_cast<SeqScanPlanNode>(plan);
            return std::make_shared<SeqScanPlanNode>(
                scan->get_output_schema_ptr(),
                scan->get_table_name(),
                scan->get_filter() ? std::make_shared<Expression>(*scan->get_filter()) : nullptr
            );
        }
        case PlanType::INDEX_SCAN: {
            auto scan = std::static_pointer_cast<IndexScanPlanNode>(plan);
            return std::make_shared<IndexScanPlanNode>(
                scan->get_output_schema_ptr(),
                scan->get_table_name(),
                scan->get_lookup_key(),
                scan->get_filter() ? std::make_shared<Expression>(*scan->get_filter()) : nullptr
            );
        }
        case PlanType::INSERT: {
            auto ins = std::static_pointer_cast<InsertPlanNode>(plan);
            return std::make_shared<InsertPlanNode>(ins->get_table_name(), ins->get_values());
        }
        case PlanType::UPDATE: {
            auto upd = std::static_pointer_cast<UpdatePlanNode>(plan);
            return std::make_shared<UpdatePlanNode>(
                upd->get_table_name(),
                upd->get_target_col(),
                upd->get_new_val(),
                upd->get_filter() ? std::make_shared<Expression>(*upd->get_filter()) : nullptr
            );
        }
        case PlanType::DELETE_PLAN: {
            auto del = std::static_pointer_cast<DeletePlanNode>(plan);
            return std::make_shared<DeletePlanNode>(
                del->get_table_name(),
                del->get_filter() ? std::make_shared<Expression>(*del->get_filter()) : nullptr
            );
        }
        case PlanType::NESTED_LOOP_JOIN: {
            auto join = std::static_pointer_cast<NestedLoopJoinPlanNode>(plan);
            const Expression* cond = join->get_join_cond();
            if (cond && cond->type == ExpressionType::COMPARE_EQ) {
                if (cond->left && cond->left->type == ExpressionType::COL_REF &&
                    cond->right && cond->right->type == ExpressionType::COL_REF) {
                    return std::make_shared<HashJoinPlanNode>(
                        join->get_output_schema_ptr(),
                        optimized_children[0],
                        optimized_children[1],
                        cond->left->col_name,
                        cond->right->col_name
                    );
                }
            }
            return std::make_shared<NestedLoopJoinPlanNode>(
                join->get_output_schema_ptr(),
                optimized_children[0],
                optimized_children[1],
                join->get_join_cond() ? std::make_shared<Expression>(*join->get_join_cond()) : nullptr
            );
        }
        case PlanType::SORT: {
            auto sort = std::static_pointer_cast<SortPlanNode>(plan);
            return std::make_shared<SortPlanNode>(
                sort->get_output_schema_ptr(),
                optimized_children[0],
                sort->get_sort_cols(),
                sort->get_order_asc()
            );
        }
        case PlanType::LIMIT: {
            auto lim = std::static_pointer_cast<LimitPlanNode>(plan);
            return std::make_shared<LimitPlanNode>(
                lim->get_output_schema_ptr(),
                optimized_children[0],
                lim->get_limit(),
                lim->get_offset()
            );
        }
        default:
            return plan;
    }
}

std::shared_ptr<AbstractPlanNode> Optimizer::optimize_index_scan(std::shared_ptr<AbstractPlanNode> plan) {
    if (!plan) return nullptr;

    // Recursively optimize children first
    std::vector<std::shared_ptr<AbstractPlanNode>> optimized_children;
    for (const auto& child : plan->get_children()) {
        optimized_children.push_back(optimize_index_scan(child));
    }

    if (plan->get_type() == PlanType::SEQ_SCAN) {
        auto scan = std::static_pointer_cast<SeqScanPlanNode>(plan);
        TableMetadata* table = catalog_.get_table(scan->get_table_name());
        if (table && scan->get_filter()) {
            const Expression* filter = scan->get_filter();
            
            // Check for equality filter on primary key
            if (filter->type == ExpressionType::COMPARE_EQ) {
                Value key;
                bool match = false;
                if (filter->left && filter->left->type == ExpressionType::COL_REF &&
                    filter->right && filter->right->type == ExpressionType::LITERAL) {
                    int idx = table->schema.get_col_index(filter->left->col_name);
                    if (idx >= 0) {
                        std::vector<IndexMetadata*> idxs = catalog_.get_table_indexes(scan->get_table_name());
                        for (auto* idx_meta : idxs) {
                            if (idx_meta->key_attrs.size() == 1 && idx_meta->key_attrs[0] == idx) {
                                key = filter->right->literal_val;
                                match = true;
                                break;
                            }
                        }
                    }
                } else if (filter->right && filter->right->type == ExpressionType::COL_REF &&
                           filter->left && filter->left->type == ExpressionType::LITERAL) {
                    int idx = table->schema.get_col_index(filter->right->col_name);
                    if (idx >= 0) {
                        std::vector<IndexMetadata*> idxs = catalog_.get_table_indexes(scan->get_table_name());
                        for (auto* idx_meta : idxs) {
                            if (idx_meta->key_attrs.size() == 1 && idx_meta->key_attrs[0] == idx) {
                                key = filter->left->literal_val;
                                match = true;
                                break;
                            }
                        }
                    }
                }

                if (match) {
                    return std::make_shared<IndexScanPlanNode>(
                        scan->get_output_schema_ptr(),
                        scan->get_table_name(),
                        key,
                        scan->get_filter() ? std::make_shared<Expression>(*scan->get_filter()) : nullptr
                    );
                }
            }
        }
    }

    // Reconstruct parents with optimized children
    if (!optimized_children.empty()) {
        switch (plan->get_type()) {
            case PlanType::NESTED_LOOP_JOIN: {
                auto join = std::static_pointer_cast<NestedLoopJoinPlanNode>(plan);
                return std::make_shared<NestedLoopJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_join_cond() ? std::make_shared<Expression>(*join->get_join_cond()) : nullptr
                );
            }
            case PlanType::HASH_JOIN: {
                auto join = std::static_pointer_cast<HashJoinPlanNode>(plan);
                return std::make_shared<HashJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_left_col(),
                    join->get_right_col()
                );
            }
            case PlanType::SORT: {
                auto sort = std::static_pointer_cast<SortPlanNode>(plan);
                return std::make_shared<SortPlanNode>(
                    sort->get_output_schema_ptr(),
                    optimized_children[0],
                    sort->get_sort_cols(),
                    sort->get_order_asc()
                );
            }
            case PlanType::LIMIT: {
                auto lim = std::static_pointer_cast<LimitPlanNode>(plan);
                return std::make_shared<LimitPlanNode>(
                    lim->get_output_schema_ptr(),
                    optimized_children[0],
                    lim->get_limit(),
                    lim->get_offset()
                );
            }
            default:
                break;
        }
    }

    return plan;
}

std::shared_ptr<AbstractPlanNode> Optimizer::optimize_projection_pruning(std::shared_ptr<AbstractPlanNode> plan) {
    if (!plan) return nullptr;

    std::vector<std::shared_ptr<AbstractPlanNode>> optimized_children;
    for (const auto& child : plan->get_children()) {
        optimized_children.push_back(optimize_projection_pruning(child));
    }

    if (!optimized_children.empty()) {
        switch (plan->get_type()) {
            case PlanType::NESTED_LOOP_JOIN: {
                auto join = std::static_pointer_cast<NestedLoopJoinPlanNode>(plan);
                return std::make_shared<NestedLoopJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_join_cond() ? std::make_shared<Expression>(*join->get_join_cond()) : nullptr
                );
            }
            case PlanType::HASH_JOIN: {
                auto join = std::static_pointer_cast<HashJoinPlanNode>(plan);
                return std::make_shared<HashJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_left_col(),
                    join->get_right_col()
                );
            }
            case PlanType::SORT: {
                auto sort = std::static_pointer_cast<SortPlanNode>(plan);
                return std::make_shared<SortPlanNode>(
                    sort->get_output_schema_ptr(),
                    optimized_children[0],
                    sort->get_sort_cols(),
                    sort->get_order_asc()
                );
            }
            case PlanType::LIMIT: {
                auto lim = std::static_pointer_cast<LimitPlanNode>(plan);
                return std::make_shared<LimitPlanNode>(
                    lim->get_output_schema_ptr(),
                    optimized_children[0],
                    lim->get_limit(),
                    lim->get_offset()
                );
            }
            default:
                break;
        }
    }

    return plan;
}

std::shared_ptr<AbstractPlanNode> Optimizer::optimize_join_selection(std::shared_ptr<AbstractPlanNode> plan) {
    if (!plan) return nullptr;

    std::vector<std::shared_ptr<AbstractPlanNode>> optimized_children;
    for (const auto& child : plan->get_children()) {
        optimized_children.push_back(optimize_join_selection(child));
    }

    if (plan->get_type() == PlanType::NESTED_LOOP_JOIN) {
        auto join = std::static_pointer_cast<NestedLoopJoinPlanNode>(plan);
        const Expression* cond = join->get_join_cond();
        if (cond && cond->type == ExpressionType::COMPARE_EQ) {
            if (cond->left && cond->left->type == ExpressionType::COL_REF &&
                cond->right && cond->right->type == ExpressionType::COL_REF) {
                
                // Construct NestedLoopJoin candidate
                auto nlj_candidate = std::make_shared<NestedLoopJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_join_cond() ? std::make_shared<Expression>(*join->get_join_cond()) : nullptr
                );

                // Construct HashJoin candidate
                auto hj_candidate = std::make_shared<HashJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    cond->left->col_name,
                    cond->right->col_name
                );

                double nlj_cost = estimate_cost(nlj_candidate.get());
                double hj_cost = estimate_cost(hj_candidate.get());

                if (hj_cost < nlj_cost) {
                    return hj_candidate;
                } else {
                    return nlj_candidate;
                }
            }
        }
    }

    if (!optimized_children.empty()) {
        switch (plan->get_type()) {
            case PlanType::NESTED_LOOP_JOIN: {
                auto join = std::static_pointer_cast<NestedLoopJoinPlanNode>(plan);
                return std::make_shared<NestedLoopJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_join_cond() ? std::make_shared<Expression>(*join->get_join_cond()) : nullptr
                );
            }
            case PlanType::HASH_JOIN: {
                auto join = std::static_pointer_cast<HashJoinPlanNode>(plan);
                return std::make_shared<HashJoinPlanNode>(
                    join->get_output_schema_ptr(),
                    optimized_children[0],
                    optimized_children[1],
                    join->get_left_col(),
                    join->get_right_col()
                );
            }
            case PlanType::SORT: {
                auto sort = std::static_pointer_cast<SortPlanNode>(plan);
                return std::make_shared<SortPlanNode>(
                    sort->get_output_schema_ptr(),
                    optimized_children[0],
                    sort->get_sort_cols(),
                    sort->get_order_asc()
                );
            }
            case PlanType::LIMIT: {
                auto lim = std::static_pointer_cast<LimitPlanNode>(plan);
                return std::make_shared<LimitPlanNode>(
                    lim->get_output_schema_ptr(),
                    optimized_children[0],
                    lim->get_limit(),
                    lim->get_offset()
                );
            }
            default:
                break;
        }
    }

    return plan;
}

int64_t Optimizer::estimate_cardinality(const AbstractPlanNode* plan) {
    if (!plan) return 0;

    switch (plan->get_type()) {
        case PlanType::SEQ_SCAN: {
            const auto* scan = static_cast<const SeqScanPlanNode*>(plan);
            const TableMetadata* table = catalog_.get_table(scan->get_table_name());
            if (!table) return 1000;
            int64_t rows = table->stats.num_rows > 0 ? table->stats.num_rows : 1000;
            if (scan->get_filter()) {
                if (scan->get_filter()->type == ExpressionType::COMPARE_EQ) {
                    return std::max<int64_t>(1, rows / 10);
                }
                return std::max<int64_t>(1, rows / 3);
            }
            return rows;
        }
        case PlanType::INDEX_SCAN: {
            const auto* scan = static_cast<const IndexScanPlanNode*>(plan);
            // Index scan matches point key query
            return 1;
        }
        case PlanType::NESTED_LOOP_JOIN:
        case PlanType::HASH_JOIN: {
            int64_t left_card = estimate_cardinality(plan->get_children()[0].get());
            int64_t right_card = estimate_cardinality(plan->get_children()[1].get());
            return std::max<int64_t>(1, (left_card * right_card) / 10);
        }
        case PlanType::SORT:
            return estimate_cardinality(plan->get_children()[0].get());
        case PlanType::LIMIT: {
            const auto* lim = static_cast<const LimitPlanNode*>(plan);
            int64_t child_card = estimate_cardinality(plan->get_children()[0].get());
            return std::min<int64_t>(child_card, lim->get_limit());
        }
        default:
            if (!plan->get_children().empty()) {
                return estimate_cardinality(plan->get_children()[0].get());
            }
            break;
    }
    return 1000;
}

double Optimizer::estimate_cost(const AbstractPlanNode* plan) {
    if (!plan) return 0.0;

    switch (plan->get_type()) {
        case PlanType::SEQ_SCAN: {
            const auto* scan = static_cast<const SeqScanPlanNode*>(plan);
            int64_t rows = estimate_cardinality(scan);
            const TableMetadata* table = catalog_.get_table(scan->get_table_name());
            int64_t avg_row_size = table ? (table->stats.avg_row_size > 0 ? table->stats.avg_row_size : 100) : 100;
            double pages = std::max<double>(1.0, (rows * avg_row_size) / static_cast<double>(PAGE_SIZE));
            return pages * SEQ_PAGE_COST + rows * CPU_TUPLE_COST;
        }
        case PlanType::INDEX_SCAN: {
            const auto* scan = static_cast<const IndexScanPlanNode*>(plan);
            int64_t rows = estimate_cardinality(scan);
            return 3.0 * RANDOM_PAGE_COST + rows * CPU_TUPLE_COST + rows * RANDOM_PAGE_COST;
        }
        case PlanType::NESTED_LOOP_JOIN: {
            double left_cost = estimate_cost(plan->get_children()[0].get());
            double right_cost = estimate_cost(plan->get_children()[1].get());
            int64_t left_rows = estimate_cardinality(plan->get_children()[0].get());
            int64_t right_rows = estimate_cardinality(plan->get_children()[1].get());
            return left_cost + left_rows * right_cost + (left_rows * right_rows) * CPU_OPERATOR_COST;
        }
        case PlanType::HASH_JOIN: {
            double left_cost = estimate_cost(plan->get_children()[0].get());
            double right_cost = estimate_cost(plan->get_children()[1].get());
            int64_t left_rows = estimate_cardinality(plan->get_children()[0].get());
            int64_t right_rows = estimate_cardinality(plan->get_children()[1].get());
            return left_cost + right_cost + (left_rows + right_rows) * CPU_TUPLE_COST;
        }
        case PlanType::SORT: {
            double child_cost = estimate_cost(plan->get_children()[0].get());
            int64_t rows = estimate_cardinality(plan->get_children()[0].get());
            return child_cost + rows * std::log2(rows + 1.0) * CPU_OPERATOR_COST;
        }
        case PlanType::LIMIT: {
            return estimate_cost(plan->get_children()[0].get()) + CPU_TUPLE_COST;
        }
        default:
            if (!plan->get_children().empty()) {
                return estimate_cost(plan->get_children()[0].get()) + CPU_OPERATOR_COST;
            }
            break;
    }
    return 10.0;
}

} // namespace luminadb
