#ifndef LUMINADB_QUERY_OPTIMIZER_H
#define LUMINADB_QUERY_OPTIMIZER_H

#include <memory>
#include "luminadb/query/planner.h"
#include "luminadb/core/catalog.h"

namespace luminadb {

class Optimizer {
public:
    explicit Optimizer(const Catalog& catalog) : catalog_(catalog) {}

    std::shared_ptr<AbstractPlanNode> optimize(std::shared_ptr<AbstractPlanNode> plan);

    // Cost model constants
    static constexpr double SEQ_PAGE_COST = 1.0;
    static constexpr double RANDOM_PAGE_COST = 4.0;
    static constexpr double CPU_TUPLE_COST = 0.01;
    static constexpr double CPU_OPERATOR_COST = 0.0025;

    double estimate_cost(const AbstractPlanNode* plan);
    int64_t estimate_cardinality(const AbstractPlanNode* plan);

private:
    std::shared_ptr<AbstractPlanNode> optimize_filter_pushdown(std::shared_ptr<AbstractPlanNode> plan);
    std::shared_ptr<AbstractPlanNode> optimize_index_scan(std::shared_ptr<AbstractPlanNode> plan);
    std::shared_ptr<AbstractPlanNode> optimize_projection_pruning(std::shared_ptr<AbstractPlanNode> plan);
    std::shared_ptr<AbstractPlanNode> optimize_join_selection(std::shared_ptr<AbstractPlanNode> plan);

    const Catalog& catalog_;
};

} // namespace luminadb

#endif // LUMINADB_QUERY_OPTIMIZER_H
