#include "test_framework.h"
#include "luminadb/db.h"
#include <cstdio>

namespace luminadb {
namespace test {

TEST(DBQueryTest, EndToEndFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_end_to_end";

    // Clear previous files
    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        
        // 1. Create table
        auto res = db->execute("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, rating FLOAT)");
        ASSERT_TRUE(res.rows.empty());
        ASSERT_EQ(res.message, "Table users created successfully.");

        // 2. Insert records
        res = db->execute("INSERT INTO users VALUES (1, 'Alice', 4.5)");
        res = db->execute("INSERT INTO users VALUES (2, 'Bob', 3.8)");
        res = db->execute("INSERT INTO users VALUES (3, 'Charlie', 5.0)");

        // 3. Select all (Seq Scan)
        res = db->execute("SELECT * FROM users");
        ASSERT_EQ(res.rows.size(), 3);
        ASSERT_EQ(res.column_names.size(), 3);
        ASSERT_EQ(res.rows[0][0].as_int(), 1);
        ASSERT_EQ(res.rows[0][1].as_string(), "Alice");
        ASSERT_TRUE(res.rows[0][2].as_float() == 4.5);

        // 4. Select optimized (Index Scan)
        res = db->execute("SELECT name FROM users WHERE id = 2");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.column_names.size(), 1);
        ASSERT_EQ(res.rows[0][0].as_string(), "Bob");

        // 5. Update record
        res = db->execute("UPDATE users SET rating = 4.2 WHERE id = 2");
        
        // Verify update
        res = db->execute("SELECT rating FROM users WHERE id = 2");
        ASSERT_TRUE(res.rows[0][0].as_float() == 4.2);

        // 6. Delete record
        res = db->execute("DELETE FROM users WHERE id = 1");
        
        res = db->execute("SELECT * FROM users");
        ASSERT_EQ(res.rows.size(), 2); // 1 deleted, 2 remains
    } // Closes DB

    // 7. Verify recovery & index rebuild
    {
        // Reopen DB
        auto db = DB::open(db_dir, db_name);
        
        // Check records are recovered and index is rebuilt
        auto res = db->execute("SELECT name FROM users WHERE id = 2");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.rows[0][0].as_string(), "Bob");

        res = db->execute("SELECT rating FROM users WHERE id = 2");
        ASSERT_TRUE(res.rows[0][0].as_float() == 4.2);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, TransactionRollback) {
    std::string db_dir = ".";
    std::string db_name = "test_rollback";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE products (id INT PRIMARY KEY, name VARCHAR, qty INT)");

        db->execute("INSERT INTO products VALUES (10, 'Laptop', 50)");

        // Attempt duplicate PK insert -> triggers abort and rollback
        auto res = db->execute("INSERT INTO products VALUES (10, 'Tablet', 25)");
        ASSERT_TRUE(res.message.find("Execution Error") != std::string::npos);

        // Verify laptops quantity is unchanged and tablet not inserted
        res = db->execute("SELECT * FROM products");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.rows[0][0].as_int(), 10);
        ASSERT_EQ(res.rows[0][1].as_string(), "Laptop");
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, HashJoinExecution) {
    std::string db_dir = ".";
    std::string db_name = "test_hash_join";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE orders (id INT PRIMARY KEY, amount FLOAT)");
        db->execute("CREATE TABLE items (item_id INT PRIMARY KEY, name VARCHAR, order_id INT)");

        db->execute("INSERT INTO orders VALUES (1, 100.5)");
        db->execute("INSERT INTO orders VALUES (2, 50.2)");

        db->execute("INSERT INTO items VALUES (10, 'Book', 1)");
        db->execute("INSERT INTO items VALUES (20, 'Pen', 1)");
        db->execute("INSERT INTO items VALUES (30, 'Desk', 3)"); // orphan, won't match order

        // Run join query
        auto res = db->execute("SELECT * FROM orders JOIN items ON id = order_id");

        if (res.rows.size() != 2) {
            std::printf("DEBUG MSG: %s\n", res.message.c_str());
        }
        ASSERT_EQ(res.rows.size(), 2);
        
        // Assert schema details: 5 columns
        ASSERT_EQ(res.column_names.size(), 5);
        ASSERT_EQ(res.column_names[0], "id");
        ASSERT_EQ(res.column_names[1], "amount");
        ASSERT_EQ(res.column_names[2], "item_id");
        ASSERT_EQ(res.column_names[3], "name");
        ASSERT_EQ(res.column_names[4], "order_id");

        // Assert row content (e.g. check first match)
        ASSERT_EQ(res.rows[0][0].as_int(), 1);
        ASSERT_TRUE(res.rows[0][1].as_float() == 100.5);
        ASSERT_EQ(res.rows[0][2].as_int(), 10);
        ASSERT_EQ(res.rows[0][3].as_string(), "Book");
        ASSERT_EQ(res.rows[0][4].as_int(), 1);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, AnalyzeStatementFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_analyze";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE stats_test (id INT PRIMARY KEY, name VARCHAR)");
        db->execute("INSERT INTO stats_test VALUES (1, 'One')");
        db->execute("INSERT INTO stats_test VALUES (2, 'Two')");
        db->execute("INSERT INTO stats_test VALUES (3, 'Three')");

        auto res = db->execute("ANALYZE TABLE stats_test");
        ASSERT_TRUE(res.message.find("analyzed. Rows: 3") != std::string::npos);
    }

    // Reopen DB and check if stats are loaded
    {
        auto db = DB::open(db_dir, db_name);
        // We can just verify it analyzed successfully again
        auto res = db->execute("ANALYZE TABLE stats_test");
        ASSERT_TRUE(res.message.find("analyzed. Rows: 3") != std::string::npos);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, ConstraintsValidation) {
    std::string db_dir = ".";
    std::string db_name = "test_constraints";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE constraints_test (id INT PRIMARY KEY, name VARCHAR UNIQUE, amount FLOAT NOT NULL, status INT DEFAULT 42)");

        // 1. Insert valid row (id=1, name='Alice', amount=10.5, status=1)
        auto res = db->execute("INSERT INTO constraints_test VALUES (1, 'Alice', 10.5, 1)");
        ASSERT_TRUE(res.message.find("successful") != std::string::npos);

        // 2. Test UNIQUE constraint violation: duplicate name 'Alice'
        res = db->execute("INSERT INTO constraints_test VALUES (2, 'Alice', 20.5, 1)");
        ASSERT_TRUE(res.message.find("Duplicate unique key") != std::string::npos);

        // 3. Test NOT NULL constraint violation: null amount
        res = db->execute("INSERT INTO constraints_test VALUES (3, 'Bob', NULL, 1)");
        ASSERT_TRUE(res.message.find("NOT NULL constraint violated") != std::string::npos);

        // 4. Test DEFAULT value: missing status, should default to 42
        res = db->execute("INSERT INTO constraints_test VALUES (4, 'Charlie', 30.5)");
        ASSERT_TRUE(res.message.find("successful") != std::string::npos);

        // Retrieve and assert status is 42
        res = db->execute("SELECT * FROM constraints_test");
        // Charlie should be row index 1 (since Bob and duplicate Alice failed)
        // Rows: (1, 'Alice', 10.5, 1), (4, 'Charlie', 30.5, 42)
        ASSERT_EQ(res.rows.size(), 2);
        ASSERT_EQ(res.rows[1][0].as_int(), 4);
        ASSERT_EQ(res.rows[1][1].as_string(), "Charlie");
        ASSERT_TRUE(res.rows[1][2].as_float() == 30.5);
        ASSERT_EQ(res.rows[1][3].as_int(), 42);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, ExplainStatementFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_explain";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE explain_test (id INT PRIMARY KEY, val INT)");

        auto res = db->execute("EXPLAIN SELECT * FROM explain_test WHERE id = 10");
        ASSERT_TRUE(res.message.find("IndexScan") != std::string::npos);

        res = db->execute("EXPLAIN SELECT * FROM explain_test");
        ASSERT_TRUE(res.message.find("SeqScan") != std::string::npos);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, CreateIndexFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_create_index";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE idx_test (id INT PRIMARY KEY, name VARCHAR, val INT)");
        db->execute("INSERT INTO idx_test VALUES (1, 'One', 10)");
        db->execute("INSERT INTO idx_test VALUES (2, 'Two', 20)");

        // Initially EXPLAIN SELECT * FROM idx_test WHERE val = 10 should be a SeqScan
        auto res = db->execute("EXPLAIN SELECT * FROM idx_test WHERE val = 10");
        ASSERT_TRUE(res.message.find("SeqScan") != std::string::npos);

        // Create the index
        res = db->execute("CREATE INDEX idx_val ON idx_test (val)");
        ASSERT_TRUE(res.message.find("created successfully") != std::string::npos);

        // Now EXPLAIN SELECT * FROM idx_test WHERE val = 10 should be optimized to an IndexScan!
        res = db->execute("EXPLAIN SELECT * FROM idx_test WHERE val = 10");
        ASSERT_TRUE(res.message.find("IndexScan") != std::string::npos);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, ExplainAnalyzeFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_explain_analyze";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE ea_test (id INT PRIMARY KEY, name VARCHAR)");
        db->execute("INSERT INTO ea_test VALUES (1, 'Alice')");
        db->execute("INSERT INTO ea_test VALUES (2, 'Bob')");
        db->execute("INSERT INTO ea_test VALUES (3, 'Charlie')");

        auto res = db->execute("EXPLAIN ANALYZE SELECT * FROM ea_test WHERE id = 2");
        ASSERT_TRUE(res.message.find("IndexScan") != std::string::npos);
        ASSERT_TRUE(res.message.find("actual rows=1") != std::string::npos);
        ASSERT_TRUE(res.message.find("loops=2") != std::string::npos); // 1 match + 1 false end
        ASSERT_TRUE(res.message.find("exec_time=") != std::string::npos);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, QueryCacheFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_cache_flow";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE qc_test (id INT PRIMARY KEY, val INT)");

        // 1. Initial INSERT
        db->execute("INSERT INTO qc_test VALUES (1, 100)");
        ASSERT_EQ(db->get_query_cache().size(), 0);

        // 2. First SELECT: should populate cache
        auto res = db->execute("SELECT * FROM qc_test");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(db->get_query_cache().size(), 1);

        // 3. Second SELECT: cache hit
        res = db->execute("SELECT * FROM qc_test");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(db->get_query_cache().size(), 1);

        // 4. INSERT: should invalidate cache
        db->execute("INSERT INTO qc_test VALUES (2, 200)");
        ASSERT_EQ(db->get_query_cache().size(), 0);

        // 5. Third SELECT: repopulate
        res = db->execute("SELECT * FROM qc_test");
        ASSERT_EQ(res.rows.size(), 2);
        ASSERT_EQ(db->get_query_cache().size(), 1);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, SystemTablesFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_system_tables";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE sys_table_test (id INT PRIMARY KEY, name VARCHAR, val FLOAT)");
        db->execute("INSERT INTO sys_table_test VALUES (1, 'TestItem', 12.3)");
        db->execute("CREATE INDEX idx_sys_val ON sys_table_test (val)");

        // 1. Query system_tables
        auto res = db->execute("SELECT * FROM system_tables");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.rows[0][0].as_string(), "sys_table_test");

        // 2. Query system_columns
        res = db->execute("SELECT * FROM system_columns");
        ASSERT_EQ(res.rows.size(), 3);
        ASSERT_EQ(res.rows[0][0].as_string(), "sys_table_test");
        ASSERT_EQ(res.rows[0][1].as_string(), "id");
        ASSERT_EQ(res.rows[0][2].as_string(), "INT");
        ASSERT_EQ(res.rows[0][4].as_int(), 1); // primary key

        ASSERT_EQ(res.rows[1][1].as_string(), "name");
        ASSERT_EQ(res.rows[1][2].as_string(), "VARCHAR");
        ASSERT_EQ(res.rows[1][4].as_int(), 0);

        // 3. Query system_indexes
        res = db->execute("SELECT * FROM system_indexes");
        // Should return two indexes: primary key index (sys_table_test_pk_idx) and custom index (idx_sys_val)
        ASSERT_EQ(res.rows.size(), 2);
        ASSERT_EQ(res.rows[1][0].as_string(), "idx_sys_val");
        ASSERT_EQ(res.rows[1][1].as_string(), "sys_table_test");
        ASSERT_EQ(res.rows[1][2].as_string(), "val");
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, TransactionSavepointsFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_savepoints";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE sp_test (id INT PRIMARY KEY, val INT)");

        // Insert row 1
        db->execute("INSERT INTO sp_test VALUES (1, 10)");

        // Savepoint 1
        auto res = db->execute("SAVEPOINT sp1");
        ASSERT_TRUE(res.message.find("created") != std::string::npos);

        // Insert row 2
        db->execute("INSERT INTO sp_test VALUES (2, 20)");

        // Savepoint 2
        res = db->execute("SAVEPOINT sp2");
        ASSERT_TRUE(res.message.find("created") != std::string::npos);

        // Insert row 3
        db->execute("INSERT INTO sp_test VALUES (3, 30)");

        // Currently we should see 3 rows
        res = db->execute("SELECT * FROM sp_test");
        ASSERT_EQ(res.rows.size(), 3);

        // Rollback to sp2 (removes row 3)
        res = db->execute("ROLLBACK TO sp2");
        ASSERT_TRUE(res.message.find("successful") != std::string::npos);

        res = db->execute("SELECT * FROM sp_test");
        ASSERT_EQ(res.rows.size(), 2);
        ASSERT_EQ(res.rows[0][0].as_int(), 1);
        ASSERT_EQ(res.rows[1][0].as_int(), 2);

        // Rollback to sp1 (removes row 2)
        res = db->execute("ROLLBACK TO sp1");
        ASSERT_TRUE(res.message.find("successful") != std::string::npos);

        res = db->execute("SELECT * FROM sp_test");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.rows[0][0].as_int(), 1);

        // Release sp1
        res = db->execute("RELEASE SAVEPOINT sp1");
        ASSERT_TRUE(res.message.find("released") != std::string::npos);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

TEST(DBQueryTest, AggregationExecutorsFlow) {
    std::string db_dir = ".";
    std::string db_name = "test_aggregations";

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    {
        auto db = DB::open(db_dir, db_name);
        db->execute("CREATE TABLE agg_test (id INT PRIMARY KEY, val INT, score FLOAT)");
        db->execute("INSERT INTO agg_test VALUES (1, 10, 1.5)");
        db->execute("INSERT INTO agg_test VALUES (2, 20, 2.5)");
        db->execute("INSERT INTO agg_test VALUES (3, 30, 3.5)");
        db->execute("INSERT INTO agg_test VALUES (4, NULL, NULL)");

        auto res = db->execute("SELECT COUNT(*), COUNT(val), SUM(val), AVG(val), MIN(val), MAX(val), AVG(score) FROM agg_test");
        ASSERT_EQ(res.rows.size(), 1);
        ASSERT_EQ(res.column_names.size(), 7);

        // 1. COUNT(*)
        ASSERT_EQ(res.rows[0][0].as_int(), 4);
        // 2. COUNT(val)
        ASSERT_EQ(res.rows[0][1].as_int(), 3);
        // 3. SUM(val)
        ASSERT_EQ(res.rows[0][2].as_int(), 60);
        // 4. AVG(val)
        ASSERT_TRUE(res.rows[0][3].as_float() == 20.0);
        // 5. MIN(val)
        ASSERT_EQ(res.rows[0][4].as_int(), 10);
        // 6. MAX(val)
        ASSERT_EQ(res.rows[0][5].as_int(), 30);
        // 7. AVG(score)
        ASSERT_TRUE(res.rows[0][6].as_float() == 2.5);
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
}

} // namespace test
} // namespace luminadb
