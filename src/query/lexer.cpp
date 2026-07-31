#include "luminadb/query/lexer.h"
#include <cctype>
#include <algorithm>

namespace luminadb {

std::string_view token_type_to_string(TokenType type) {
    switch (type) {
        case TokenType::KEYWORD_SELECT: return "SELECT";
        case TokenType::KEYWORD_FROM: return "FROM";
        case TokenType::KEYWORD_WHERE: return "WHERE";
        case TokenType::KEYWORD_INSERT: return "INSERT";
        case TokenType::KEYWORD_INTO: return "INTO";
        case TokenType::KEYWORD_VALUES: return "VALUES";
        case TokenType::KEYWORD_UPDATE: return "UPDATE";
        case TokenType::KEYWORD_SET: return "SET";
        case TokenType::KEYWORD_DELETE: return "DELETE";
        case TokenType::KEYWORD_CREATE: return "CREATE";
        case TokenType::KEYWORD_TABLE: return "TABLE";
        case TokenType::KEYWORD_INT: return "INT";
        case TokenType::KEYWORD_FLOAT: return "FLOAT";
        case TokenType::KEYWORD_VARCHAR: return "VARCHAR";
        case TokenType::KEYWORD_BLOB: return "BLOB";
        case TokenType::KEYWORD_PRIMARY: return "PRIMARY";
        case TokenType::KEYWORD_KEY: return "KEY";
        case TokenType::KEYWORD_NULL: return "NULL";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::LITERAL_INT: return "LITERAL_INT";
        case TokenType::LITERAL_FLOAT: return "LITERAL_FLOAT";
        case TokenType::LITERAL_STRING: return "LITERAL_STRING";
        case TokenType::OP_EQ: return "=";
        case TokenType::OP_NEQ: return "!=";
        case TokenType::OP_LT: return "<";
        case TokenType::OP_LTE: return "<=";
        case TokenType::OP_GT: return ">";
        case TokenType::OP_GTE: return ">=";
        case TokenType::STAR: return "*";
        case TokenType::COMMA: return ",";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

char Lexer::peek() const {
    if (cursor_ >= source_.size()) return '\0';
    return source_[cursor_];
}

char Lexer::peek_next() const {
    if (cursor_ + 1 >= source_.size()) return '\0';
    return source_[cursor_ + 1];
}

char Lexer::advance() {
    if (cursor_ >= source_.size()) return '\0';
    return source_[cursor_++];
}

void Lexer::skip_whitespace() {
    while (cursor_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[cursor_]))) {
        cursor_++;
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = next_token();
        tokens.push_back(tok);
        if (tok.type == TokenType::END_OF_FILE || tok.type == TokenType::ERROR) {
            break;
        }
    }
    return tokens;
}

Token Lexer::next_token() {
    skip_whitespace();
    size_t start_pos = cursor_;

    if (cursor_ >= source_.size()) {
        return Token{TokenType::END_OF_FILE, "", start_pos};
    }

    char c = advance();

    // Check operators and punctuation
    switch (c) {
        case '*': return Token{TokenType::STAR, "*", start_pos};
        case ',': return Token{TokenType::COMMA, ",", start_pos};
        case '(': return Token{TokenType::LPAREN, "(", start_pos};
        case ')': return Token{TokenType::RPAREN, ")", start_pos};
        case '+': return Token{TokenType::OP_PLUS, "+", start_pos};
        case '-': return Token{TokenType::OP_MINUS, "-", start_pos};
        case '/': return Token{TokenType::OP_SLASH, "/", start_pos};
        case '%': return Token{TokenType::OP_PERCENT, "%", start_pos};
        case '=': return Token{TokenType::OP_EQ, "=", start_pos};
        case '!':
            if (peek() == '=') {
                advance();
                return Token{TokenType::OP_NEQ, "!=", start_pos};
            }
            return Token{TokenType::ERROR, "Unexpected character !", start_pos};
        case '<':
            if (peek() == '>') {
                advance();
                return Token{TokenType::OP_NEQ, "<>", start_pos};
            }
            if (peek() == '=') {
                advance();
                return Token{TokenType::OP_LTE, "<=", start_pos};
            }
            return Token{TokenType::OP_LT, "<", start_pos};
        case '>':
            if (peek() == '=') {
                advance();
                return Token{TokenType::OP_GTE, ">=", start_pos};
            }
            return Token{TokenType::OP_GT, ">", start_pos};
        case '\'': { // String literal
            std::string str_val;
            while (cursor_ < source_.size() && source_[cursor_] != '\'') {
                str_val += advance();
            }
            if (cursor_ >= source_.size()) {
                return Token{TokenType::ERROR, "Unterminated string literal", start_pos};
            }
            advance(); // Consume closing quote
            return Token{TokenType::LITERAL_STRING, str_val, start_pos};
        }
    }

    // Number literal
    if (std::isdigit(static_cast<unsigned char>(c))) {
        std::string num_str;
        num_str += c;
        bool is_float = false;
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
            if (peek() == '.') {
                if (is_float) {
                    // Double dot in number
                    return Token{TokenType::ERROR, "Malformed float literal", start_pos};
                }
                is_float = true;
            }
            num_str += advance();
        }
        return Token{is_float ? TokenType::LITERAL_FLOAT : TokenType::LITERAL_INT, num_str, start_pos};
    }

    // Identifier or Keyword
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string ident;
        ident += c;
        while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
            ident += advance();
        }

        // Map keywords (case-insensitive)
        std::string upper_ident = ident;
        std::transform(upper_ident.begin(), upper_ident.end(), upper_ident.begin(), [](unsigned char cc) {
            return std::toupper(cc);
        });

        if (upper_ident == "SELECT") return Token{TokenType::KEYWORD_SELECT, ident, start_pos};
        if (upper_ident == "FROM") return Token{TokenType::KEYWORD_FROM, ident, start_pos};
        if (upper_ident == "WHERE") return Token{TokenType::KEYWORD_WHERE, ident, start_pos};
        if (upper_ident == "INSERT") return Token{TokenType::KEYWORD_INSERT, ident, start_pos};
        if (upper_ident == "INTO") return Token{TokenType::KEYWORD_INTO, ident, start_pos};
        if (upper_ident == "VALUES") return Token{TokenType::KEYWORD_VALUES, ident, start_pos};
        if (upper_ident == "UPDATE") return Token{TokenType::KEYWORD_UPDATE, ident, start_pos};
        if (upper_ident == "SET") return Token{TokenType::KEYWORD_SET, ident, start_pos};
        if (upper_ident == "DELETE") return Token{TokenType::KEYWORD_DELETE, ident, start_pos};
        if (upper_ident == "CREATE") return Token{TokenType::KEYWORD_CREATE, ident, start_pos};
        if (upper_ident == "TABLE") return Token{TokenType::KEYWORD_TABLE, ident, start_pos};
        if (upper_ident == "ANALYZE") return Token{TokenType::KEYWORD_ANALYZE, ident, start_pos};
        if (upper_ident == "EXPLAIN") return Token{TokenType::KEYWORD_EXPLAIN, ident, start_pos};
        if (upper_ident == "INDEX") return Token{TokenType::KEYWORD_INDEX, ident, start_pos};
        if (upper_ident == "INT") return Token{TokenType::KEYWORD_INT, ident, start_pos};
        if (upper_ident == "FLOAT") return Token{TokenType::KEYWORD_FLOAT, ident, start_pos};
        if (upper_ident == "VARCHAR") return Token{TokenType::KEYWORD_VARCHAR, ident, start_pos};
        if (upper_ident == "BLOB") return Token{TokenType::KEYWORD_BLOB, ident, start_pos};
        if (upper_ident == "PRIMARY") return Token{TokenType::KEYWORD_PRIMARY, ident, start_pos};
        if (upper_ident == "KEY") return Token{TokenType::KEYWORD_KEY, ident, start_pos};
        if (upper_ident == "NULL") return Token{TokenType::KEYWORD_NULL, ident, start_pos};
        if (upper_ident == "JOIN") return Token{TokenType::KEYWORD_JOIN, ident, start_pos};
        if (upper_ident == "ON") return Token{TokenType::KEYWORD_ON, ident, start_pos};
        if (upper_ident == "ORDER") return Token{TokenType::KEYWORD_ORDER, ident, start_pos};
        if (upper_ident == "BY") return Token{TokenType::KEYWORD_BY, ident, start_pos};
        if (upper_ident == "ASC") return Token{TokenType::KEYWORD_ASC, ident, start_pos};
        if (upper_ident == "DESC") return Token{TokenType::KEYWORD_DESC, ident, start_pos};
        if (upper_ident == "LIMIT") return Token{TokenType::KEYWORD_LIMIT, ident, start_pos};
        if (upper_ident == "OFFSET") return Token{TokenType::KEYWORD_OFFSET, ident, start_pos};
        if (upper_ident == "LIKE") return Token{TokenType::KEYWORD_LIKE, ident, start_pos};
        if (upper_ident == "AND") return Token{TokenType::KEYWORD_AND, ident, start_pos};
        if (upper_ident == "OR") return Token{TokenType::KEYWORD_OR, ident, start_pos};
        if (upper_ident == "NOT") return Token{TokenType::KEYWORD_NOT, ident, start_pos};
        if (upper_ident == "UNIQUE") return Token{TokenType::KEYWORD_UNIQUE, ident, start_pos};
        if (upper_ident == "DEFAULT") return Token{TokenType::KEYWORD_DEFAULT, ident, start_pos};
        if (upper_ident == "SAVEPOINT") return Token{TokenType::KEYWORD_SAVEPOINT, ident, start_pos};
        if (upper_ident == "RELEASE") return Token{TokenType::KEYWORD_RELEASE, ident, start_pos};
        if (upper_ident == "TO") return Token{TokenType::KEYWORD_TO, ident, start_pos};
        if (upper_ident == "ROLLBACK") return Token{TokenType::KEYWORD_ROLLBACK, ident, start_pos};

        return Token{TokenType::IDENTIFIER, ident, start_pos};
    }

    std::string err_msg = "Unexpected character: ";
    err_msg += c;
    return Token{TokenType::ERROR, err_msg, start_pos};
}

} // namespace luminadb
