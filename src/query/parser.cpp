#include "luminadb/query/parser.h"
#include <sstream>
#include <algorithm>

namespace luminadb {

Parser::Parser(std::string source) : lexer_(std::move(source)) {}

Token Parser::peek() const {
    if (cursor_ >= tokens_.size()) {
        return Token{TokenType::END_OF_FILE, "", 0};
    }
    return tokens_[cursor_];
}

Token Parser::advance() {
    Token current = peek();
    if (cursor_ < tokens_.size()) {
        cursor_++;
    }
    return current;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

void Parser::consume(TokenType type, std::string_view err_msg) {
    if (check(type)) {
        advance();
        return;
    }
    std::ostringstream ss;
    ss << err_msg << " (Got '" << peek().text << "' at position " << peek().position << ")";
    throw DBException(StatusCode::ERR_PARSE_ERROR, ss.str());
}

std::unique_ptr<Statement> Parser::parse() {
    tokens_ = lexer_.tokenize();
    cursor_ = 0;

    if (tokens_.empty() || check(TokenType::END_OF_FILE)) {
        throw DBException(StatusCode::ERR_PARSE_ERROR, "Empty query source");
    }

    if (peek().type == TokenType::ERROR) {
        throw DBException(StatusCode::ERR_PARSE_ERROR, "Lexer error: " + peek().text);
    }

    if (match(TokenType::KEYWORD_SELECT)) {
        return parse_select();
    } else if (match(TokenType::KEYWORD_INSERT)) {
        return parse_insert();
    } else if (match(TokenType::KEYWORD_UPDATE)) {
        return parse_update();
    } else if (match(TokenType::KEYWORD_DELETE)) {
        return parse_delete();
    } else if (match(TokenType::KEYWORD_CREATE)) {
        if (match(TokenType::KEYWORD_INDEX)) {
            return parse_create_index();
        }
        return parse_create_table();
    } else if (match(TokenType::KEYWORD_ANALYZE)) {
        return parse_analyze();
    } else if (match(TokenType::KEYWORD_EXPLAIN)) {
        return parse_explain();
    } else if (match(TokenType::KEYWORD_SAVEPOINT)) {
        return parse_savepoint();
    } else if (match(TokenType::KEYWORD_ROLLBACK)) {
        return parse_rollback();
    } else if (match(TokenType::KEYWORD_RELEASE)) {
        return parse_release();
    }

    throw DBException(StatusCode::ERR_PARSE_ERROR, "Unexpected statement beginning: " + peek().text);
}

std::unique_ptr<Statement> Parser::parse_create_index() {
    consume(TokenType::IDENTIFIER, "Expected index name after 'CREATE INDEX'");
    std::string idx_name = tokens_[cursor_ - 1].text;
    consume(TokenType::KEYWORD_ON, "Expected 'ON' after index name");
    consume(TokenType::IDENTIFIER, "Expected table name after 'ON'");
    std::string tbl_name = tokens_[cursor_ - 1].text;
    consume(TokenType::LPAREN, "Expected '(' before column name");
    consume(TokenType::IDENTIFIER, "Expected column name inside '('");
    std::string col_name = tokens_[cursor_ - 1].text;
    consume(TokenType::RPAREN, "Expected ')' after column name");
    match(TokenType::END_OF_FILE);
    return std::make_unique<CreateIndexStatement>(std::move(idx_name), std::move(tbl_name), std::move(col_name));
}

std::unique_ptr<Statement> Parser::parse_explain() {
    bool analyze = false;
    if (match(TokenType::KEYWORD_ANALYZE)) {
        analyze = true;
    }
    // Parse the inner statement recursively (e.g. SELECT)
    if (match(TokenType::KEYWORD_SELECT)) {
        auto inner = parse_select();
        return std::make_unique<ExplainStatement>(std::move(inner), analyze);
    }
    throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected 'SELECT' after 'EXPLAIN'");
}

std::unique_ptr<Statement> Parser::parse_analyze() {
    consume(TokenType::KEYWORD_TABLE, "Expected 'TABLE' after 'ANALYZE'");
    consume(TokenType::IDENTIFIER, "Expected table name after 'ANALYZE TABLE'");
    std::string t_name = tokens_[cursor_ - 1].text;
    match(TokenType::END_OF_FILE);
    return std::make_unique<AnalyzeStatement>(std::move(t_name));
}

std::unique_ptr<Statement> Parser::parse_create_table() {
    consume(TokenType::KEYWORD_TABLE, "Expected 'TABLE' after 'CREATE'");
    consume(TokenType::IDENTIFIER, "Expected table name after 'CREATE TABLE'");
    std::string t_name = tokens_[cursor_ - 1].text;

    consume(TokenType::LPAREN, "Expected '(' to begin columns list");

    std::vector<ColumnDef> cols;
    while (true) {
        consume(TokenType::IDENTIFIER, "Expected column name");
        std::string col_name = tokens_[cursor_ - 1].text;

        DataType col_type = DataType::NONE;
        if (match(TokenType::KEYWORD_INT)) {
            col_type = DataType::INT;
        } else if (match(TokenType::KEYWORD_FLOAT)) {
            col_type = DataType::FLOAT;
        } else if (match(TokenType::KEYWORD_VARCHAR)) {
            col_type = DataType::VARCHAR;
        } else if (match(TokenType::KEYWORD_BLOB)) {
            col_type = DataType::BLOB;
        } else {
            throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected column type (INT, FLOAT, VARCHAR, BLOB) for: " + col_name);
        }

        bool is_pk = false;
        bool is_not_null = false;
        bool is_unique = false;
        bool has_default = false;
        Value default_val;

        while (true) {
            if (match(TokenType::KEYWORD_PRIMARY)) {
                consume(TokenType::KEYWORD_KEY, "Expected 'KEY' after 'PRIMARY'");
                is_pk = true;
            } else if (match(TokenType::KEYWORD_NOT)) {
                consume(TokenType::KEYWORD_NULL, "Expected 'NULL' after 'NOT'");
                is_not_null = true;
            } else if (match(TokenType::KEYWORD_UNIQUE)) {
                is_unique = true;
            } else if (match(TokenType::KEYWORD_DEFAULT)) {
                has_default = true;
                if (match(TokenType::LITERAL_INT)) {
                    default_val = Value(std::stoll(tokens_[cursor_ - 1].text));
                } else if (match(TokenType::LITERAL_FLOAT)) {
                    default_val = Value(std::stod(tokens_[cursor_ - 1].text));
                } else if (match(TokenType::LITERAL_STRING)) {
                    default_val = Value(tokens_[cursor_ - 1].text);
                } else if (match(TokenType::KEYWORD_NULL)) {
                    default_val = Value();
                } else {
                    throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected literal value after 'DEFAULT'");
                }
            } else {
                break;
            }
        }

        cols.push_back(ColumnDef{std::move(col_name), col_type, is_pk, is_not_null, is_unique, has_default, std::move(default_val)});

        if (match(TokenType::COMMA)) {
            continue;
        }
        if (check(TokenType::RPAREN)) {
            break;
        }
        throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected ',' or ')' in columns list");
    }

    consume(TokenType::RPAREN, "Expected ')' to close columns list");

    // Optional semicolon
    match(TokenType::END_OF_FILE); // Consume trailing spaces/EOF

    return std::make_unique<CreateTableStatement>(std::move(t_name), std::move(cols));
}

std::unique_ptr<Statement> Parser::parse_select() {
    std::vector<std::string> fields;
    bool is_aggregate = false;
    std::vector<std::string> agg_fields;
    std::vector<AggregationType> agg_types;

    if (match(TokenType::STAR)) {
        // SELECT *
    } else {
        while (true) {
            if (check(TokenType::IDENTIFIER)) {
                std::string ident = peek().text;
                std::string upper_ident = ident;
                std::transform(upper_ident.begin(), upper_ident.end(), upper_ident.begin(), ::toupper);
                
                if (upper_ident == "COUNT" || upper_ident == "SUM" || upper_ident == "AVG" || upper_ident == "MIN" || upper_ident == "MAX") {
                    consume(TokenType::IDENTIFIER, "Expected function name");
                    consume(TokenType::LPAREN, "Expected '(' after function name");
                    
                    std::string arg;
                    if (match(TokenType::STAR)) {
                        arg = "*";
                    } else {
                        consume(TokenType::IDENTIFIER, "Expected column name inside function");
                        arg = tokens_[cursor_ - 1].text;
                    }
                    consume(TokenType::RPAREN, "Expected ')' after function argument");

                    is_aggregate = true;
                    agg_fields.push_back(std::move(arg));
                    
                    AggregationType atype = AggregationType::COUNT;
                    if (upper_ident == "SUM") atype = AggregationType::SUM;
                    else if (upper_ident == "AVG") atype = AggregationType::AVG;
                    else if (upper_ident == "MIN") atype = AggregationType::MIN;
                    else if (upper_ident == "MAX") atype = AggregationType::MAX;
                    
                    agg_types.push_back(atype);
                    
                    std::string field_name = upper_ident + "(" + agg_fields.back() + ")";
                    std::transform(field_name.begin(), field_name.end(), field_name.begin(), ::tolower);
                    fields.push_back(std::move(field_name));
                } else {
                    consume(TokenType::IDENTIFIER, "Expected column name");
                    fields.push_back(tokens_[cursor_ - 1].text);
                }
            } else {
                consume(TokenType::IDENTIFIER, "Expected column name");
                fields.push_back(tokens_[cursor_ - 1].text);
            }

            if (match(TokenType::COMMA)) {
                continue;
            }
            break;
        }
    }

    consume(TokenType::KEYWORD_FROM, "Expected 'FROM' in SELECT statement");
    consume(TokenType::IDENTIFIER, "Expected table name after 'FROM'");
    std::string t_name = tokens_[cursor_ - 1].text;

    bool has_join = false;
    std::string join_table;
    std::string join_left_col;
    std::string join_right_col;

    if (match(TokenType::KEYWORD_JOIN)) {
        consume(TokenType::IDENTIFIER, "Expected join table name");
        join_table = tokens_[cursor_ - 1].text;
        consume(TokenType::KEYWORD_ON, "Expected 'ON' in join");
        
        consume(TokenType::IDENTIFIER, "Expected left join column name");
        join_left_col = tokens_[cursor_ - 1].text;
        
        consume(TokenType::OP_EQ, "Expected '=' in join condition");
        
        consume(TokenType::IDENTIFIER, "Expected right join column name");
        join_right_col = tokens_[cursor_ - 1].text;
        
        has_join = true;
    }

    std::shared_ptr<Expression> where;
    if (match(TokenType::KEYWORD_WHERE)) {
        where = parse_expression();
    }

    auto select_stmt = std::make_unique<SelectStatement>(std::move(t_name), std::move(fields), std::move(where));
    select_stmt->has_join = has_join;
    select_stmt->join_table = std::move(join_table);
    select_stmt->join_left_col = std::move(join_left_col);
    select_stmt->join_right_col = std::move(join_right_col);
    select_stmt->is_aggregate = is_aggregate;
    select_stmt->agg_fields = std::move(agg_fields);
    select_stmt->agg_types = std::move(agg_types);
    return select_stmt;
}

std::unique_ptr<Statement> Parser::parse_insert() {
    consume(TokenType::KEYWORD_INTO, "Expected 'INTO' after 'INSERT'");
    consume(TokenType::IDENTIFIER, "Expected table name");
    std::string t_name = tokens_[cursor_ - 1].text;

    consume(TokenType::KEYWORD_VALUES, "Expected 'VALUES' in INSERT statement");
    consume(TokenType::LPAREN, "Expected '(' before values list");

    std::vector<Value> vals;
    while (true) {
        if (match(TokenType::KEYWORD_NULL)) {
            vals.push_back(Value());
        } else if (match(TokenType::LITERAL_INT)) {
            std::string text = tokens_[cursor_ - 1].text;
            vals.push_back(Value(std::stoll(text)));
        } else if (match(TokenType::LITERAL_FLOAT)) {
            std::string text = tokens_[cursor_ - 1].text;
            vals.push_back(Value(std::stod(text)));
        } else if (match(TokenType::LITERAL_STRING)) {
            std::string text = tokens_[cursor_ - 1].text;
            vals.push_back(Value(text));
        } else {
            throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected literal value in INSERT list");
        }

        if (match(TokenType::COMMA)) {
            continue;
        }
        if (check(TokenType::RPAREN)) {
            break;
        }
        throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected ',' or ')' in values list");
    }

    consume(TokenType::RPAREN, "Expected ')' to close values list");

    return std::make_unique<InsertStatement>(std::move(t_name), std::move(vals));
}

std::unique_ptr<Statement> Parser::parse_update() {
    consume(TokenType::IDENTIFIER, "Expected table name");
    std::string t_name = tokens_[cursor_ - 1].text;

    consume(TokenType::KEYWORD_SET, "Expected 'SET' in UPDATE statement");
    consume(TokenType::IDENTIFIER, "Expected target column name");
    std::string target_col = tokens_[cursor_ - 1].text;

    consume(TokenType::OP_EQ, "Expected '=' in SET clause");

    Value new_val;
    if (match(TokenType::KEYWORD_NULL)) {
        new_val = Value();
    } else if (match(TokenType::LITERAL_INT)) {
        new_val = Value(std::stoll(tokens_[cursor_ - 1].text));
    } else if (match(TokenType::LITERAL_FLOAT)) {
        new_val = Value(std::stod(tokens_[cursor_ - 1].text));
    } else if (match(TokenType::LITERAL_STRING)) {
        new_val = Value(tokens_[cursor_ - 1].text);
    } else {
        throw DBException(StatusCode::ERR_PARSE_ERROR, "Expected literal value for update value");
    }

    std::shared_ptr<Expression> where;
    if (match(TokenType::KEYWORD_WHERE)) {
        where = parse_expression();
    }

    return std::make_unique<UpdateStatement>(std::move(t_name), std::move(target_col), std::move(new_val), std::move(where));
}

std::unique_ptr<Statement> Parser::parse_delete() {
    consume(TokenType::KEYWORD_FROM, "Expected 'FROM' after 'DELETE'");
    consume(TokenType::IDENTIFIER, "Expected table name");
    std::string t_name = tokens_[cursor_ - 1].text;

    std::shared_ptr<Expression> where;
    if (match(TokenType::KEYWORD_WHERE)) {
        where = parse_expression();
    }

    return std::make_unique<DeleteStatement>(std::move(t_name), std::move(where));
}

std::shared_ptr<Expression> Parser::parse_expression() {
    return parse_or();
}

std::shared_ptr<Expression> Parser::parse_or() {
    auto left = parse_and();
    while (match(TokenType::KEYWORD_OR)) {
        auto right = parse_and();
        left = std::make_shared<Expression>(Expression{ExpressionType::LOGICAL_OR, Value(), "", left, right});
    }
    return left;
}

std::shared_ptr<Expression> Parser::parse_and() {
    auto left = parse_not();
    while (match(TokenType::KEYWORD_AND)) {
        auto right = parse_not();
        left = std::make_shared<Expression>(Expression{ExpressionType::LOGICAL_AND, Value(), "", left, right});
    }
    return left;
}

std::shared_ptr<Expression> Parser::parse_not() {
    if (match(TokenType::KEYWORD_NOT)) {
        auto child = parse_not();
        return std::make_shared<Expression>(Expression{ExpressionType::LOGICAL_NOT, Value(), "", child, nullptr});
    }
    return parse_comparison();
}

std::shared_ptr<Expression> Parser::parse_comparison() {
    auto left = parse_term();
    ExpressionType op_type;
    bool has_op = false;

    if (match(TokenType::OP_EQ)) { op_type = ExpressionType::COMPARE_EQ; has_op = true; }
    else if (match(TokenType::OP_NEQ)) { op_type = ExpressionType::COMPARE_NEQ; has_op = true; }
    else if (match(TokenType::OP_LT)) { op_type = ExpressionType::COMPARE_LT; has_op = true; }
    else if (match(TokenType::OP_LTE)) { op_type = ExpressionType::COMPARE_LTE; has_op = true; }
    else if (match(TokenType::OP_GT)) { op_type = ExpressionType::COMPARE_GT; has_op = true; }
    else if (match(TokenType::OP_GTE)) { op_type = ExpressionType::COMPARE_GTE; has_op = true; }

    if (has_op) {
        auto right = parse_term();
        return std::make_shared<Expression>(Expression{op_type, Value(), "", left, right});
    }
    return left;
}

std::shared_ptr<Expression> Parser::parse_term() {
    return parse_factor();
}

std::shared_ptr<Expression> Parser::parse_factor() {
    return parse_primary();
}

std::shared_ptr<Expression> Parser::parse_primary() {
    if (match(TokenType::LPAREN)) {
        auto expr = parse_expression();
        consume(TokenType::RPAREN, "Expected ')' to match '('");
        return expr;
    }

    if (match(TokenType::KEYWORD_NULL)) {
        return std::make_shared<Expression>(Expression{ExpressionType::LITERAL, Value(), "", nullptr, nullptr});
    }
    if (match(TokenType::LITERAL_INT)) {
        return std::make_shared<Expression>(Expression{ExpressionType::LITERAL, Value(std::stoll(tokens_[cursor_-1].text)), "", nullptr, nullptr});
    }
    if (match(TokenType::LITERAL_FLOAT)) {
        return std::make_shared<Expression>(Expression{ExpressionType::LITERAL, Value(std::stod(tokens_[cursor_-1].text)), "", nullptr, nullptr});
    }
    if (match(TokenType::LITERAL_STRING)) {
        return std::make_shared<Expression>(Expression{ExpressionType::LITERAL, Value(tokens_[cursor_-1].text), "", nullptr, nullptr});
    }
    if (match(TokenType::IDENTIFIER)) {
        return std::make_shared<Expression>(Expression{ExpressionType::COL_REF, Value(), tokens_[cursor_-1].text, nullptr, nullptr});
    }

    throw DBException(StatusCode::ERR_PARSE_ERROR, "Unexpected primary token in expression: " + peek().text);
}

std::unique_ptr<Statement> Parser::parse_savepoint() {
    consume(TokenType::IDENTIFIER, "Expected savepoint name");
    std::string name = tokens_[cursor_ - 1].text;
    match(TokenType::END_OF_FILE);
    return std::make_unique<SavepointStatement>(std::move(name));
}

std::unique_ptr<Statement> Parser::parse_rollback() {
    consume(TokenType::KEYWORD_TO, "Expected 'TO' after 'ROLLBACK'");
    match(TokenType::KEYWORD_SAVEPOINT);
    consume(TokenType::IDENTIFIER, "Expected savepoint name");
    std::string name = tokens_[cursor_ - 1].text;
    match(TokenType::END_OF_FILE);
    return std::make_unique<RollbackToStatement>(std::move(name));
}

std::unique_ptr<Statement> Parser::parse_release() {
    consume(TokenType::KEYWORD_SAVEPOINT, "Expected 'SAVEPOINT' after 'RELEASE'");
    consume(TokenType::IDENTIFIER, "Expected savepoint name");
    std::string name = tokens_[cursor_ - 1].text;
    match(TokenType::END_OF_FILE);
    return std::make_unique<ReleaseSavepointStatement>(std::move(name));
}

} // namespace luminadb
