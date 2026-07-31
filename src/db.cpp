#include "luminadb/db.h"
#include "luminadb/query/parser.h"
#include "luminadb/query/planner.h"
#include "luminadb/query/optimizer.h"
#include <fstream>
#include <iostream>
#include <set>

namespace luminadb {

DB::DB(std::string db_file, std::string log_file, std::string catalog_file, size_t bpm_pool_size)
    : db_file_(std::move(db_file)), log_file_(std::move(log_file)), catalog_file_(std::move(catalog_file)), bpm_pool_size_(bpm_pool_size) {}

DB::~DB() {
    if (current_txn_) {
        txn_manager_->commit(current_txn_);
    }
    catalog_.reset();
    txn_manager_.reset();
    lock_manager_.reset();
    wal_manager_.reset();
    bpm_.reset();
    disk_manager_.reset();
}

std::unique_ptr<DB> DB::open(std::string db_dir, std::string db_name, size_t bpm_pool_size) {
    std::string sep = "/";
    if (!db_dir.empty() && (db_dir.back() == '/' || db_dir.back() == '\\')) {
        sep = "";
    }
    std::string db_file = db_dir + sep + db_name + ".db";
    std::string log_file = db_dir + sep + db_name + ".wal";
    std::string catalog_file = db_dir + sep + db_name + ".catalog";

    auto db = std::unique_ptr<DB>(new DB(db_file, log_file, catalog_file, bpm_pool_size));
    db->initialize();
    return db;
}

void DB::initialize() {
    disk_manager_ = std::make_unique<DiskManager>(db_file_);
    bpm_ = std::make_unique<BufferPoolManager>(bpm_pool_size_, *disk_manager_);
    wal_manager_ = std::make_unique<WALManager>(log_file_);
    lock_manager_ = std::make_unique<LockManager>();
    txn_manager_ = std::make_unique<TransactionManager>(*lock_manager_, *wal_manager_);
    catalog_ = std::make_unique<Catalog>();

    load_catalog();
    recover();
    rebuild_indexes();
}

void DB::load_catalog() {
    std::ifstream f(catalog_file_, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;

    std::streamsize size = f.tellg();
    if (size <= 0) return;
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    f.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!f.bad()) {
        try {
            catalog_->deserialize(buffer.data(), buffer.size(), *disk_manager_);
        } catch (...) {
            catalog_ = std::make_unique<Catalog>();
        }
    }
}

void DB::save_catalog() {
    if (!catalog_) return;
    std::ofstream f(catalog_file_, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        throw DBException(StatusCode::ERR_IO_ERROR, "Failed to write catalog file: " + catalog_file_);
    }
    std::vector<uint8_t> serialized = catalog_->serialize();
    f.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
}

void DB::rebuild_indexes() {
    for (const auto& t_name : catalog_->get_table_names()) {
        TableMetadata* table = catalog_->get_table(t_name);
        if (!table) continue;

        // Clear and rebuild BTreeIndex if primary key exists
        int pk_idx = -1;
        for (size_t i = 0; i < table->schema.columns().size(); ++i) {
            if (table->schema.columns()[i].is_primary_key) {
                pk_idx = static_cast<int>(i);
                break;
            }
        }

        // Search if indexes are registered. Otherwise, auto-build primary index
        std::vector<IndexMetadata*> idxs = catalog_->get_table_indexes(t_name);
        if (idxs.empty() && pk_idx >= 0) {
            catalog_->create_index(t_name + "_pk_idx", t_name, {pk_idx});
            idxs = catalog_->get_table_indexes(t_name);
        }

        for (auto* idx_meta : idxs) {
            idx_meta->index->clear();
        }

        record_id_t rid = table->table_heap->get_first_record_id();
        while (get_page_id(rid) != INVALID_PAGE_ID) {
            std::vector<uint8_t> rec_data;
            if (table->table_heap->get_record(rid, rec_data)) {
                try {
                    std::vector<Value> row = table->schema.deserialize_row(rec_data.data(), rec_data.size());
                    for (auto* idx_meta : idxs) {
                        // For simplicity, handle single column index keys
                        int attr = idx_meta->key_attrs[0];
                        idx_meta->index->insert(row[attr], rid);
                    }
                } catch (...) {
                    // Skip corrupt rows
                }
            }
            rid = table->table_heap->get_next_record_id(rid);
        }
    }
}

void DB::recover() {
    auto apply_physical_image = [this](page_id_t pid, uint32_t sid, const std::vector<uint8_t>& image) {
        if (pid < 0 || pid >= disk_manager_->num_pages()) return;
        
        // Fetch via BufferPoolManager to maintain cache coherence
        uint8_t* page_data = bpm_->fetch_page(pid);
        if (!page_data) return;

        if (image.empty()) {
            Page::delete_record(page_data, sid);
        } else {
            Page::update_record(page_data, sid, image.data(), image.size());
        }
        
        bpm_->unpin_page(pid, true); // Mark dirty and unpin
    };

    auto records = wal_manager_->read_all_records();
    if (records.empty()) return;

    std::set<txn_id_t> active_txns;
    std::set<txn_id_t> committed_txns;
    std::set<txn_id_t> aborted_txns;

    for (const auto& rec : records) {
        if (rec.type == LogRecordType::BEGIN) {
            active_txns.insert(rec.txn_id);
        } else if (rec.type == LogRecordType::COMMIT) {
            active_txns.erase(rec.txn_id);
            committed_txns.insert(rec.txn_id);
        } else if (rec.type == LogRecordType::ABORT) {
            active_txns.erase(rec.txn_id);
            aborted_txns.insert(rec.txn_id);
        }
    }

    // Redo (Forward)
    for (const auto& rec : records) {
        if (rec.type == LogRecordType::UPDATE) {
            if (committed_txns.count(rec.txn_id) > 0) {
                apply_physical_image(rec.page_id, rec.slot_id, rec.after_image);
            }
        }
    }

    // Undo (Backward)
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (rec.type == LogRecordType::UPDATE) {
            if (active_txns.count(rec.txn_id) > 0 || aborted_txns.count(rec.txn_id) > 0) {
                apply_physical_image(rec.page_id, rec.slot_id, rec.before_image);
            }
        }
    }

    bpm_->flush_all_pages();
}

static std::shared_ptr<AbstractExecutor> build_executor_tree(
    const AbstractPlanNode* plan_node,
    Catalog& catalog,
    txn_id_t txn_id,
    WALManager& wal,
    TransactionManager* txn_mgr,
    bool instrument = false,
    std::unordered_map<const AbstractPlanNode*, std::shared_ptr<InstrumentedExecutor>>* inst_map = nullptr) {
    
    if (!plan_node) return nullptr;

    std::vector<std::shared_ptr<AbstractExecutor>> child_executors;
    for (const auto& child_plan : plan_node->get_children()) {
        child_executors.push_back(build_executor_tree(child_plan.get(), catalog, txn_id, wal, txn_mgr, instrument, inst_map));
    }

    std::shared_ptr<AbstractExecutor> exec;
    switch (plan_node->get_type()) {
        case PlanType::SEQ_SCAN: {
            const auto* scan = static_cast<const SeqScanPlanNode*>(plan_node);
            TableMetadata* table = catalog.get_table(scan->get_table_name());
            auto scan_executor = std::make_shared<SeqScanExecutor>(*table->table_heap, table->schema, scan->get_filter(), txn_id, txn_mgr);
            if (scan->get_output_schema().columns().size() != table->schema.columns().size()) {
                std::vector<int> proj_indices;
                for (const auto& col : scan->get_output_schema().columns()) {
                    proj_indices.push_back(table->schema.get_col_index(col.name));
                }
                exec = std::make_shared<ProjectionExecutor>(scan_executor, scan->get_output_schema(), proj_indices);
            } else {
                exec = scan_executor;
            }
            break;
        }
        case PlanType::INDEX_SCAN: {
            const auto* scan = static_cast<const IndexScanPlanNode*>(plan_node);
            TableMetadata* table = catalog.get_table(scan->get_table_name());
            std::vector<IndexMetadata*> idxs = catalog.get_table_indexes(scan->get_table_name());
            if (idxs.empty()) {
                throw DBException(StatusCode::ERR_INTERNAL, "Optimizer error: index scan planned but index missing");
            }
            auto scan_executor = std::make_shared<IndexScanExecutor>(*table->table_heap, *idxs[0]->index, table->schema, scan->get_lookup_key(), scan->get_filter(), txn_id, txn_mgr);
            if (scan->get_output_schema().columns().size() != table->schema.columns().size()) {
                std::vector<int> proj_indices;
                for (const auto& col : scan->get_output_schema().columns()) {
                    proj_indices.push_back(table->schema.get_col_index(col.name));
                }
                exec = std::make_shared<ProjectionExecutor>(scan_executor, scan->get_output_schema(), proj_indices);
            } else {
                exec = scan_executor;
            }
            break;
        }
        case PlanType::INSERT: {
            const auto* ins = static_cast<const InsertPlanNode*>(plan_node);
            TableMetadata* table = catalog.get_table(ins->get_table_name());
            std::vector<IndexMetadata*> idxs = catalog.get_table_indexes(ins->get_table_name());
            
            // Dummy empty index if no primary key index is built
            static BTreeIndex dummy_idx;
            BTreeIndex* idx_ptr = &dummy_idx;
            if (!idxs.empty()) {
                idx_ptr = idxs[0]->index.get();
            }
            exec = std::make_shared<InsertExecutor>(*table->table_heap, *idx_ptr, wal, table->schema, ins->get_values(), txn_id);
            break;
        }
        case PlanType::UPDATE: {
            const auto* upd = static_cast<const UpdatePlanNode*>(plan_node);
            TableMetadata* table = catalog.get_table(upd->get_table_name());
            std::vector<IndexMetadata*> idxs = catalog.get_table_indexes(upd->get_table_name());
            
            static BTreeIndex dummy_idx;
            BTreeIndex* idx_ptr = &dummy_idx;
            if (!idxs.empty()) {
                idx_ptr = idxs[0]->index.get();
            }

            // Update needs scan node as child
            Schema scan_schema = table->schema;
            auto scan_child = std::make_shared<SeqScanExecutor>(*table->table_heap, table->schema, upd->get_filter());

            exec = std::make_shared<UpdateExecutor>(*table->table_heap, *idx_ptr, wal, table->schema, upd->get_target_col(), upd->get_new_val(), std::move(scan_child), txn_id);
            break;
        }
        case PlanType::DELETE_PLAN: {
            const auto* del = static_cast<const DeletePlanNode*>(plan_node);
            TableMetadata* table = catalog.get_table(del->get_table_name());
            std::vector<IndexMetadata*> idxs = catalog.get_table_indexes(del->get_table_name());
            
            static BTreeIndex dummy_idx;
            BTreeIndex* idx_ptr = &dummy_idx;
            if (!idxs.empty()) {
                idx_ptr = idxs[0]->index.get();
            }

            auto scan_child = std::make_shared<SeqScanExecutor>(*table->table_heap, table->schema, del->get_filter());

            exec = std::make_shared<DeleteExecutor>(*table->table_heap, *idx_ptr, wal, table->schema, std::move(scan_child), txn_id);
            break;
        }
        case PlanType::NESTED_LOOP_JOIN: {
            const auto* join = static_cast<const NestedLoopJoinPlanNode*>(plan_node);
            exec = std::make_shared<NestedLoopJoinExecutor>(child_executors[0], child_executors[1], join->get_join_cond(), join->get_output_schema());
            break;
        }
        case PlanType::HASH_JOIN: {
            const auto* join = static_cast<const HashJoinPlanNode*>(plan_node);
            exec = std::make_shared<HashJoinExecutor>(child_executors[0], child_executors[1], join->get_left_col(), join->get_right_col(), join->get_output_schema());
            break;
        }
        case PlanType::SORT: {
            const auto* sort = static_cast<const SortPlanNode*>(plan_node);
            exec = std::make_shared<SortExecutor>(child_executors[0], sort->get_sort_cols(), sort->get_order_asc());
            break;
        }
        case PlanType::LIMIT: {
            const auto* lim = static_cast<const LimitPlanNode*>(plan_node);
            exec = std::make_shared<LimitExecutor>(child_executors[0], lim->get_limit(), lim->get_offset());
            break;
        }
        case PlanType::SYSTEM_SCAN: {
            const auto* scan = static_cast<const SystemScanPlanNode*>(plan_node);
            exec = std::make_shared<SystemScanExecutor>(catalog, scan->get_output_schema(), scan->get_system_table_name());
            break;
        }
        case PlanType::AGGREGATION: {
            const auto* agg = static_cast<const AggregationPlanNode*>(plan_node);
            exec = std::make_shared<AggregationExecutor>(child_executors[0], agg->get_aggregates(), agg->get_types(), agg->get_output_schema());
            break;
        }
        default:
            throw DBException(StatusCode::ERR_INTERNAL, "Unreachable plan node type in builder");
    }

    if (instrument && exec) {
        auto inst = std::make_shared<InstrumentedExecutor>(exec);
        if (inst_map) {
            (*inst_map)[plan_node] = inst;
        }
        return inst;
    }
    return exec;
}

static std::string format_explain_analyze(const AbstractPlanNode* plan, const std::unordered_map<const AbstractPlanNode*, std::shared_ptr<InstrumentedExecutor>>& inst_map, int indent = 0) {
    if (!plan) return "";
    
    std::string indent_str(indent * 2, ' ');
    std::string node_desc;
    switch (plan->get_type()) {
        case PlanType::SEQ_SCAN: {
            const auto* scan = static_cast<const SeqScanPlanNode*>(plan);
            node_desc = "SeqScan(" + scan->get_table_name() + ")";
            break;
        }
        case PlanType::INDEX_SCAN: {
            const auto* scan = static_cast<const IndexScanPlanNode*>(plan);
            node_desc = "IndexScan(" + scan->get_table_name() + ", key=" + scan->get_lookup_key().to_string() + ")";
            break;
        }
        case PlanType::NESTED_LOOP_JOIN:
            node_desc = "NestedLoopJoin";
            break;
        case PlanType::HASH_JOIN: {
            const auto* join = static_cast<const HashJoinPlanNode*>(plan);
            node_desc = "HashJoin(" + join->get_left_col() + " = " + join->get_right_col() + ")";
            break;
        }
        case PlanType::SORT:
            node_desc = "Sort";
            break;
        case PlanType::LIMIT: {
            const auto* lim = static_cast<const LimitPlanNode*>(plan);
            node_desc = "Limit(limit=" + std::to_string(lim->get_limit()) + ")";
            break;
        }
        default:
            node_desc = "PlanNode";
            break;
    }
    
    std::string info = indent_str + "-> " + node_desc;
    auto iter = inst_map.find(plan);
    if (iter != inst_map.end()) {
        auto metrics = iter->second->get_metrics();
        char buf[256];
        std::sprintf(buf, " (actual rows=%lld loops=%lld init_time=%.3fms exec_time=%.3fms)",
                     (long long)metrics.rows_produced,
                     (long long)metrics.next_calls,
                     iter->second->get_init_duration(),
                     metrics.total_duration_ms);
        info += buf;
    }
    info += "\n";
    
    for (const auto& child : plan->get_children()) {
        info += format_explain_analyze(child.get(), inst_map, indent + 1);
    }
    return info;
}

QueryResult DB::execute(std::string query_str) {
    std::unique_lock<std::shared_mutex> lock(db_rw_lck_);

    Parser parser(query_str);
    std::unique_ptr<Statement> stmt;
    try {
        stmt = parser.parse();
    } catch (const DBException& e) {
        return QueryResult{{}, {}, {}, "Parse Error: " + std::string(e.what())};
    } catch (const std::exception& e) {
        return QueryResult{{}, {}, {}, "Generic Parse Error: " + std::string(e.what())};
    }

    bool is_select = false;
    if (stmt->type() == StatementType::SELECT) {
        is_select = true;
        QueryResult cached_res;
        if (query_cache_.lookup(query_str, cached_res)) {
            return cached_res;
        }
    }

    Transaction* txn = nullptr;
    if (in_savepoint_txn_ && current_txn_ != nullptr) {
        txn = current_txn_;
    } else {
        if (current_txn_ != nullptr) {
            txn_manager_->commit(current_txn_);
            current_txn_ = nullptr;
        }
        txn = txn_manager_->begin_txn();
        current_txn_ = txn;
    }
    txn_id_t txn_id = txn->get_txn_id();

    try {
        if (stmt->type() == StatementType::CREATE_TABLE) {
            auto* create_stmt = static_cast<CreateTableStatement*>(stmt.get());
            
            std::vector<Column> cols;
            for (const auto& cdef : create_stmt->columns) {
                cols.push_back(Column{cdef.name, cdef.type, cdef.is_pk, cdef.is_not_null, cdef.is_unique, cdef.has_default, cdef.default_val});
            }

            Schema s(create_stmt->table_name, std::move(cols));
            if (!catalog_->create_table(create_stmt->table_name, std::move(s), *disk_manager_)) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table already exists");
            }
            
            save_catalog();
            rebuild_indexes();

            txn_manager_->commit(txn);
            current_txn_ = nullptr;
            in_savepoint_txn_ = false;
            return QueryResult{{}, {}, {}, "Table " + create_stmt->table_name + " created successfully."};
        }

        if (stmt->type() == StatementType::CREATE_INDEX) {
            auto* create_idx_stmt = static_cast<CreateIndexStatement*>(stmt.get());
            TableMetadata* table = catalog_->get_table(create_idx_stmt->table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + create_idx_stmt->table_name);
            }

            int col_idx = table->schema.get_col_index(create_idx_stmt->col_name);
            if (col_idx < 0) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Column not found: " + create_idx_stmt->col_name);
            }

            if (!catalog_->create_index(create_idx_stmt->index_name, create_idx_stmt->table_name, {col_idx})) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Index already exists: " + create_idx_stmt->index_name);
            }

            save_catalog();
            rebuild_indexes();

            txn_manager_->commit(txn);
            current_txn_ = nullptr;
            in_savepoint_txn_ = false;
            return QueryResult{{}, {}, {}, "Index " + create_idx_stmt->index_name + " created successfully."};
        }

        if (stmt->type() == StatementType::SAVEPOINT) {
            auto* save_stmt = static_cast<SavepointStatement*>(stmt.get());
            in_savepoint_txn_ = true;
            std::vector<uint8_t> name_bytes(save_stmt->name.begin(), save_stmt->name.end());
            wal_manager_->append_record(txn_id, LogRecordType::SAVEPOINT, INVALID_PAGE_ID, 0, name_bytes, {});
            wal_manager_->flush();

            // No commit here; transaction stays active!
            return QueryResult{{}, {}, {}, "Savepoint " + save_stmt->name + " created."};
        }

        if (stmt->type() == StatementType::RELEASE_SAVEPOINT) {
            auto* release_stmt = static_cast<ReleaseSavepointStatement*>(stmt.get());
            std::vector<uint8_t> name_bytes(release_stmt->name.begin(), release_stmt->name.end());
            wal_manager_->append_record(txn_id, LogRecordType::RELEASE_SAVEPOINT, INVALID_PAGE_ID, 0, name_bytes, {});
            wal_manager_->flush();

            return QueryResult{{}, {}, {}, "Savepoint " + release_stmt->name + " released."};
        }

        if (stmt->type() == StatementType::ROLLBACK_TO) {
            auto* rollback_stmt = static_cast<RollbackToStatement*>(stmt.get());
            
            // Read WAL backwards to find target savepoint and undo updates
            auto records = wal_manager_->read_all_records();
            bool found_savepoint = false;

            for (auto it = records.rbegin(); it != records.rend(); ++it) {
                const auto& rec = *it;
                if (rec.txn_id == txn_id) {
                    if (rec.type == LogRecordType::SAVEPOINT) {
                        std::string sp_name(rec.before_image.begin(), rec.before_image.end());
                        if (sp_name == rollback_stmt->name) {
                            found_savepoint = true;
                            break;
                        }
                    } else if (rec.type == LogRecordType::UPDATE) {
                        // Undo update
                        if (rec.page_id >= 0 && rec.page_id < disk_manager_->num_pages()) {
                            uint8_t* page_data = bpm_->fetch_page(rec.page_id);
                            if (page_data) {
                                if (rec.before_image.empty()) {
                                    Page::delete_record(page_data, rec.slot_id);
                                } else {
                                    Page::update_record(page_data, rec.slot_id, rec.before_image.data(), rec.before_image.size());
                                }
                                bpm_->unpin_page(rec.page_id, true);
                            }
                        }
                    }
                }
            }

            if (!found_savepoint) {
                throw DBException(StatusCode::ERR_INTERNAL, "Savepoint not found: " + rollback_stmt->name);
            }

            std::vector<uint8_t> name_bytes(rollback_stmt->name.begin(), rollback_stmt->name.end());
            wal_manager_->append_record(txn_id, LogRecordType::ROLLBACK_TO, INVALID_PAGE_ID, 0, name_bytes, {});
            wal_manager_->flush();

            // Sync buffer cache and rebuild indexes to match reverted slot states
            bpm_->flush_all_pages();
            rebuild_indexes();
            query_cache_.clear();

            return QueryResult{{}, {}, {}, "Rollback to savepoint " + rollback_stmt->name + " successful."};
        }

        if (stmt->type() == StatementType::ANALYZE) {
            auto* analyze_stmt = static_cast<AnalyzeStatement*>(stmt.get());
            TableMetadata* table = catalog_->get_table(analyze_stmt->table_name);
            if (!table) {
                throw DBException(StatusCode::ERR_SCHEMA_VIOLATION, "Table not found: " + analyze_stmt->table_name);
            }

            int64_t total_rows = 0;
            int64_t total_size = 0;

            record_id_t rid = table->table_heap->get_first_record_id();
            while (get_page_id(rid) != INVALID_PAGE_ID) {
                std::vector<uint8_t> data;
                if (table->table_heap->get_record(rid, data)) {
                    total_rows++;
                    total_size += data.size();
                }
                rid = table->table_heap->get_next_record_id(rid);
            }

            table->stats.num_rows = total_rows;
            table->stats.avg_row_size = total_rows > 0 ? (total_size / total_rows) : 0;

            save_catalog();
            txn_manager_->commit(txn);
            current_txn_ = nullptr;
            in_savepoint_txn_ = false;

            return QueryResult{{}, {}, {}, "Table " + analyze_stmt->table_name + " analyzed. Rows: " + std::to_string(total_rows) + ", Avg Row Size: " + std::to_string(table->stats.avg_row_size)};
        }

        if (stmt->type() == StatementType::EXPLAIN) {
            auto* explain_stmt = static_cast<ExplainStatement*>(stmt.get());
            Planner planner(*catalog_);
            std::shared_ptr<AbstractPlanNode> logical_plan = planner.plan(*explain_stmt->stmt);

            Optimizer optimizer(*catalog_);
            std::shared_ptr<AbstractPlanNode> physical_plan = optimizer.optimize(logical_plan);

            if (explain_stmt->analyze) {
                std::unordered_map<const AbstractPlanNode*, std::shared_ptr<InstrumentedExecutor>> inst_map;
                auto exec = build_executor_tree(physical_plan.get(), *catalog_, txn_id, *wal_manager_, txn_manager_.get(), true, &inst_map);
                exec->init();
                std::vector<Value> row;
                record_id_t rid;
                while (exec->next(row, rid)) {
                    // pull all rows
                }
                std::string plan_str = format_explain_analyze(physical_plan.get(), inst_map);
                txn_manager_->commit(txn);
                current_txn_ = nullptr;
                in_savepoint_txn_ = false;
                return QueryResult{{}, {}, {}, plan_str};
            } else {
                std::string plan_str = physical_plan->to_string();
                txn_manager_->commit(txn);
                current_txn_ = nullptr;
                in_savepoint_txn_ = false;
                return QueryResult{{}, {}, {}, plan_str};
            }
        }

        // Generate Plan
        Planner planner(*catalog_);
        std::shared_ptr<AbstractPlanNode> logical_plan = planner.plan(*stmt);

        // Optimize Plan
        Optimizer optimizer(*catalog_);
        std::shared_ptr<AbstractPlanNode> physical_plan = optimizer.optimize(logical_plan);

        // Build Volcano Executor Tree
        std::shared_ptr<AbstractExecutor> exec = build_executor_tree(physical_plan.get(), *catalog_, txn_id, *wal_manager_, txn_manager_.get());
        exec->init();

        QueryResult res;
        
        // Populate output schema headers
        const Schema& out_schema = exec->get_schema();
        for (const auto& col : out_schema.columns()) {
            res.column_names.push_back(col.name);
            res.column_types.push_back(col.type);
        }

        // Volcano Pull Model
        std::vector<Value> row;
        record_id_t rid;
        while (exec->next(row, rid)) {
            res.rows.push_back(std::move(row));
        }

        res.message = "Execution successful. Returned " + std::to_string(res.rows.size()) + " rows.";
        
        if (is_select) {
            std::vector<std::string> referenced_tables;
            const auto& select_stmt = static_cast<const SelectStatement&>(*stmt);
            referenced_tables.push_back(select_stmt.table_name);
            if (!select_stmt.join_table.empty()) {
                referenced_tables.push_back(select_stmt.join_table);
            }
            query_cache_.put(query_str, res, referenced_tables);
        } else if (stmt->type() == StatementType::INSERT) {
            auto* ins = static_cast<InsertStatement*>(stmt.get());
            query_cache_.invalidate_table(ins->table_name);
        } else if (stmt->type() == StatementType::UPDATE) {
            auto* upd = static_cast<UpdateStatement*>(stmt.get());
            query_cache_.invalidate_table(upd->table_name);
        } else if (stmt->type() == StatementType::DELETE) {
            auto* del = static_cast<DeleteStatement*>(stmt.get());
            query_cache_.invalidate_table(del->table_name);
        }

        if (!in_savepoint_txn_) {
            txn_manager_->commit(txn);
            current_txn_ = nullptr;
        }
        bpm_->flush_all_pages();
        return res;

    } catch (const DBException& e) {
        txn_manager_->abort(txn);
        current_txn_ = nullptr;
        in_savepoint_txn_ = false;
        recover();
        rebuild_indexes();
        return QueryResult{{}, {}, {}, "Execution Error: " + std::string(e.what())};
    } catch (const std::exception& e) {
        txn_manager_->abort(txn);
        current_txn_ = nullptr;
        in_savepoint_txn_ = false;
        recover();
        rebuild_indexes();
        return QueryResult{{}, {}, {}, "Generic Execution Error: " + std::string(e.what())};
    }
}

bool DB::has_catalog() const {
    std::shared_lock<std::shared_mutex> lock(db_rw_lck_);
    return !catalog_->get_table_names().empty();
}

} // namespace luminadb
