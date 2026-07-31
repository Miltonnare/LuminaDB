#ifndef LUMINADB_QUERY_PARSER_H
#define LUMINADB_QUERY_PARSER_H

#include <vector>
#include <memory>
#include <string>
#include "luminadb/query/ast.h"
#include "luminadb/query/lexer.h"
#include "luminadb/common/errors.h"

namespace luminadb {

class Parser {
public:
    explicit Parser(std::string source);

    std::unique_ptr<Statement> parse();

private:
    Lexer lexer_;
    std::vector<Token> tokens_;
    size_t cursor_{0};

    Token peek() const;
    Token advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    void consume(TokenType type, std::string_view err_msg);

    std::unique_ptr<Statement> parse_select();
    std::unique_ptr<Statement> parse_insert();
    std::unique_ptr<Statement> parse_update();
    std::unique_ptr<Statement> parse_delete();
    std::unique_ptr<Statement> parse_create_table();
    std::unique_ptr<Statement> parse_analyze();
    std::unique_ptr<Statement> parse_explain();
    std::unique_ptr<Statement> parse_create_index();
    std::unique_ptr<Statement> parse_savepoint();
    std::unique_ptr<Statement> parse_rollback();
    std::unique_ptr<Statement> parse_release();

    std::shared_ptr<Expression> parse_expression();
    std::shared_ptr<Expression> parse_or();
    std::shared_ptr<Expression> parse_and();
    std::shared_ptr<Expression> parse_not();
    std::shared_ptr<Expression> parse_comparison();
    std::shared_ptr<Expression> parse_term();
    std::shared_ptr<Expression> parse_factor();
    std::shared_ptr<Expression> parse_primary();
};

} // namespace luminadb

#endif // LUMINADB_QUERY_PARSER_H
