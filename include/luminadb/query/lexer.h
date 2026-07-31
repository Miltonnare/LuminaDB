#ifndef LUMINADB_QUERY_LEXER_H
#define LUMINADB_QUERY_LEXER_H

#include <string>
#include <vector>
#include <string_view>

namespace luminadb {

enum class TokenType {
    KEYWORD_SELECT,
    KEYWORD_FROM,
    KEYWORD_WHERE,
    KEYWORD_INSERT,
    KEYWORD_INTO,
    KEYWORD_VALUES,
    KEYWORD_UPDATE,
    KEYWORD_SET,
    KEYWORD_DELETE,
    KEYWORD_CREATE,
    KEYWORD_TABLE,
    KEYWORD_ANALYZE,
    KEYWORD_EXPLAIN,
    KEYWORD_INDEX,
    KEYWORD_INT,
    KEYWORD_FLOAT,
    KEYWORD_VARCHAR,
    KEYWORD_BLOB,
    KEYWORD_PRIMARY,
    KEYWORD_KEY,
    KEYWORD_NULL,
    KEYWORD_JOIN,
    KEYWORD_ON,
    KEYWORD_ORDER,
    KEYWORD_BY,
    KEYWORD_ASC,
    KEYWORD_DESC,
    KEYWORD_LIMIT,
    KEYWORD_OFFSET,
    KEYWORD_LIKE,
    KEYWORD_AND,
    KEYWORD_OR,
    KEYWORD_NOT,
    KEYWORD_UNIQUE,
    KEYWORD_DEFAULT,
    KEYWORD_SAVEPOINT,
    KEYWORD_RELEASE,
    KEYWORD_TO,
    KEYWORD_ROLLBACK,

    IDENTIFIER,
    LITERAL_INT,
    LITERAL_FLOAT,
    LITERAL_STRING,

    OP_EQ,      // =
    OP_NEQ,     // != or <>
    OP_LT,      // <
    OP_LTE,     // <=
    OP_GT,      // >
    OP_GTE,     // >=
    OP_PLUS,    // +
    OP_MINUS,   // -
    OP_SLASH,   // /
    OP_PERCENT, // %

    STAR,       // *
    COMMA,      // ,
    LPAREN,     // (
    RPAREN,     // )

    END_OF_FILE,
    ERROR
};

std::string_view token_type_to_string(TokenType type);

struct Token {
    TokenType type;
    std::string text;
    size_t position;
};

class Lexer {
public:
    explicit Lexer(std::string source);

    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t cursor_{0};

    char peek() const;
    char peek_next() const;
    char advance();
    void skip_whitespace();
    Token next_token();
};

} // namespace luminadb

#endif // LUMINADB_QUERY_LEXER_H
