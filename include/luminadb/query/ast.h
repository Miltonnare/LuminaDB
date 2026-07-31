#ifndef LUMINADB_QUERY_AST_H
#define LUMINADB_QUERY_AST_H

#include <string>
#include <vector>
#include <memory>
#include "luminadb/core/value.h"

namespace luminadb {

enum class ExpressionType {
    LITERAL,
    COL_REF,
    COMPARE_EQ,
    COMPARE_NEQ,
    COMPARE_LT,
    COMPARE_LTE,
    COMPARE_GT,
    COMPARE_GTE,
    LOGICAL_AND,
    LOGICAL_OR,
    LOGICAL_NOT
};

struct Expression {
    ExpressionType type;
    Value literal_val;
    std::string col_name;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
};

enum class StatementType {
    SELECT,
    INSERT,
    UPDATE,
    DELETE,
    CREATE_TABLE,
    ANALYZE,
    EXPLAIN,
    CREATE_INDEX,
    SAVEPOINT,
    ROLLBACK_TO,
    RELEASE_SAVEPOINT
};

struct Statement {
    virtual ~Statement() = default;
    virtual StatementType type() const = 0;
};

struct ColumnDef {
    std::string name;
    DataType type;
    bool is_pk{false};
    bool is_not_null{false};
    bool is_unique{false};
    bool has_default{false};
    Value default_val;
};

struct CreateTableStatement : public Statement {
    std::string table_name;
    std::vector<ColumnDef> columns;

    CreateTableStatement(std::string name, std::vector<ColumnDef> cols)
        : table_name(std::move(name)), columns(std::move(cols)) {}

    StatementType type() const override { return StatementType::CREATE_TABLE; }
};

enum class AggregationType {
    COUNT,
    SUM,
    AVG,
    MIN,
    MAX
};

struct SelectStatement : public Statement {
    std::string table_name;
    std::vector<std::string> fields; // empty = SELECT *
    std::shared_ptr<Expression> where_clause;
    
    bool has_join{false};
    std::string join_table;
    std::string join_left_col;
    std::string join_right_col;

    // Aggregation support
    bool is_aggregate{false};
    std::vector<std::string> agg_fields;
    std::vector<AggregationType> agg_types;

    SelectStatement(std::string name, std::vector<std::string> flds, std::shared_ptr<Expression> where)
        : table_name(std::move(name)), fields(std::move(flds)), where_clause(std::move(where)) {}

    StatementType type() const override { return StatementType::SELECT; }
};

struct InsertStatement : public Statement {
    std::string table_name;
    std::vector<Value> values;

    InsertStatement(std::string name, std::vector<Value> vals)
        : table_name(std::move(name)), values(std::move(vals)) {}

    StatementType type() const override { return StatementType::INSERT; }
};

struct UpdateStatement : public Statement {
    std::string table_name;
    std::string target_col;
    Value new_val;
    std::shared_ptr<Expression> where_clause;

    UpdateStatement(std::string name, std::string target, Value val, std::shared_ptr<Expression> where)
        : table_name(std::move(name)), target_col(std::move(target)), new_val(std::move(val)), where_clause(std::move(where)) {}

    StatementType type() const override { return StatementType::UPDATE; }
};

struct DeleteStatement : public Statement {
    std::string table_name;
    std::shared_ptr<Expression> where_clause;

    DeleteStatement(std::string name, std::shared_ptr<Expression> where)
        : table_name(std::move(name)), where_clause(std::move(where)) {}

    StatementType type() const override { return StatementType::DELETE; }
};

struct AnalyzeStatement : public Statement {
    std::string table_name;

    explicit AnalyzeStatement(std::string name) : table_name(std::move(name)) {}

    StatementType type() const override { return StatementType::ANALYZE; }
};

struct ExplainStatement : public Statement {
    std::unique_ptr<Statement> stmt;
    bool analyze{false};

    ExplainStatement(std::unique_ptr<Statement> s, bool ana) : stmt(std::move(s)), analyze(ana) {}

    StatementType type() const override { return StatementType::EXPLAIN; }
};

struct CreateIndexStatement : public Statement {
    std::string index_name;
    std::string table_name;
    std::string col_name;

    CreateIndexStatement(std::string idx, std::string tbl, std::string col)
        : index_name(std::move(idx)), table_name(std::move(tbl)), col_name(std::move(col)) {}

    StatementType type() const override { return StatementType::CREATE_INDEX; }
};

struct SavepointStatement : public Statement {
    std::string name;
    explicit SavepointStatement(std::string n) : name(std::move(n)) {}
    StatementType type() const override { return StatementType::SAVEPOINT; }
};

struct RollbackToStatement : public Statement {
    std::string name;
    explicit RollbackToStatement(std::string n) : name(std::move(n)) {}
    StatementType type() const override { return StatementType::ROLLBACK_TO; }
};

struct ReleaseSavepointStatement : public Statement {
    std::string name;
    explicit ReleaseSavepointStatement(std::string n) : name(std::move(n)) {}
    StatementType type() const override { return StatementType::RELEASE_SAVEPOINT; }
};

} // namespace luminadb

#endif // LUMINADB_QUERY_AST_H
