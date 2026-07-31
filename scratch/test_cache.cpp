#include "luminadb/db.h"
#include <iostream>
#include <cstdio>

int main() {
    std::string db_dir = ".";
    std::string db_name = "test_scratch_cache";
    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());

    auto db = DB::open(db_dir, db_name);
    db->execute("CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR, rating FLOAT)");
    db->execute("INSERT INTO users VALUES (1, 'Alice', 4.5)");
    db->execute("INSERT INTO users VALUES (2, 'Bob', 3.8)");
    db->execute("INSERT INTO users VALUES (3, 'Charlie', 5.0)");

    std::cout << "Running SELECT * FROM users..." << std::endl;
    auto res = db->execute("SELECT * FROM users");
    std::cout << "Returned: " << res.rows.size() << " rows" << std::endl;

    std::cout << "Running SELECT name FROM users WHERE id = 2..." << std::endl;
    res = db->execute("SELECT name FROM users WHERE id = 2");
    std::cout << "Returned: " << res.rows.size() << " rows" << std::endl;
    for (const auto& r : res.rows) {
        std::cout << "  Row: " << r[0].as_string() << std::endl;
    }

    std::remove((db_name + ".db").c_str());
    std::remove((db_name + ".wal").c_str());
    std::remove((db_name + ".catalog").c_str());
    return 0;
}
