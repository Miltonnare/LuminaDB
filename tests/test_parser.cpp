#include "test_framework.h"
#include "luminadb/query/lexer.h"
#include "luminadb/query/parser.h"

namespace luminadb {
namespace test {

TEST(LexerTest, TokenizeSimpleSelect) {
    std::string sql = "SELECT id, name FROM users WHERE age >= 21";
    Lexer lexer(sql);
    auto tokens = lexer.tokenize();

    // EXPECT: SELECT, id, COMMA, name, FROM, users, WHERE, age, >=, 21, EOF
    ASSERT_EQ(tokens.size(), 11);
    ASSERT_TRUE(tokens[0].type == TokenType::KEYWORD_SELECT);
    ASSERT_TRUE(tokens[1].type == TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[1].text, "id");
    ASSERT_TRUE(tokens[2].type == TokenType::COMMA);
    ASSERT_TRUE(tokens[3].type == TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[3].text, "name");
    ASSERT_TRUE(tokens[4].type == TokenType::KEYWORD_FROM);
    ASSERT_TRUE(tokens[5].type == TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[5].text, "users");
    ASSERT_TRUE(tokens[6].type == TokenType::KEYWORD_WHERE);
    ASSERT_TRUE(tokens[7].type == TokenType::IDENTIFIER);
    ASSERT_EQ(tokens[7].text, "age");
    ASSERT_TRUE(tokens[8].type == TokenType::OP_GTE);
    ASSERT_TRUE(tokens[9].type == TokenType::LITERAL_INT);
    ASSERT_EQ(tokens[9].text, "21");
    ASSERT_TRUE(tokens[10].type == TokenType::END_OF_FILE);
}

TEST(ParserTest, ParseCreateTable) {
    std::string sql = "CREATE TABLE employees (emp_id INT PRIMARY KEY, name VARCHAR, salary FLOAT)";
    Parser parser(sql);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt->type() == StatementType::CREATE_TABLE);
    auto* create = static_cast<CreateTableStatement*>(stmt.get());
    ASSERT_EQ(create->table_name, "employees");
    ASSERT_EQ(create->columns.size(), 3);
    
    ASSERT_EQ(create->columns[0].name, "emp_id");
    ASSERT_TRUE(create->columns[0].type == DataType::INT);
    ASSERT_TRUE(create->columns[0].is_pk);

    ASSERT_EQ(create->columns[1].name, "name");
    ASSERT_TRUE(create->columns[1].type == DataType::VARCHAR);
    ASSERT_FALSE(create->columns[1].is_pk);

    ASSERT_EQ(create->columns[2].name, "salary");
    ASSERT_TRUE(create->columns[2].type == DataType::FLOAT);
    ASSERT_FALSE(create->columns[2].is_pk);
}

TEST(ParserTest, ParseSelect) {
    std::string sql = "SELECT * FROM items WHERE price != 99.99";
    Parser parser(sql);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt->type() == StatementType::SELECT);
    auto* select = static_cast<SelectStatement*>(stmt.get());
    ASSERT_EQ(select->table_name, "items");
    ASSERT_TRUE(select->fields.empty()); // SELECT *

    ASSERT_TRUE(select->where_clause != nullptr);
    ASSERT_TRUE(select->where_clause->type == ExpressionType::COMPARE_NEQ);
    ASSERT_TRUE(select->where_clause->left->type == ExpressionType::COL_REF);
    ASSERT_EQ(select->where_clause->left->col_name, "price");
    ASSERT_TRUE(select->where_clause->right->type == ExpressionType::LITERAL);
    ASSERT_TRUE(select->where_clause->right->literal_val == Value(99.99));
}

TEST(ParserTest, ParseInsert) {
    std::string sql = "INSERT INTO users VALUES (101, 'Alice', NULL)";
    Parser parser(sql);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt->type() == StatementType::INSERT);
    auto* insert = static_cast<InsertStatement*>(stmt.get());
    ASSERT_EQ(insert->table_name, "users");
    ASSERT_EQ(insert->values.size(), 3);
    ASSERT_EQ(insert->values[0].as_int(), 101);
    ASSERT_EQ(insert->values[1].as_string(), "Alice");
    ASSERT_TRUE(insert->values[2].is_null());
}

TEST(ParserTest, ParseErrors) {
    std::string bad_sql = "CREATE TABLE (id INT)"; // Missing table name
    Parser parser(bad_sql);
    bool caught = false;
    try {
        parser.parse();
    } catch (const DBException& e) {
        if (e.code() == StatusCode::ERR_PARSE_ERROR) {
            caught = true;
        }
    }
    ASSERT_TRUE(caught);
}

} // namespace test
} // namespace luminadb
