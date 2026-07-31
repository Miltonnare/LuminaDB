#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include "luminadb/db.h"

static void print_table(const luminadb::QueryResult& res) {
    if (res.rows.empty()) {
        std::cout << res.message << std::endl;
        return;
    }

    // Calculate column widths
    std::vector<size_t> widths;
    for (const auto& name : res.column_names) {
        widths.push_back(name.size());
    }
    
    for (const auto& row : res.rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].to_string().size());
        }
    }

    // Separator line
    auto print_sep = [&]() {
        for (size_t w : widths) {
            std::cout << "+" << std::string(w + 2, '-');
        }
        std::cout << "+" << std::endl;
    };

    print_sep();
    for (size_t i = 0; i < res.column_names.size(); ++i) {
        std::cout << "| " << res.column_names[i] 
                  << std::string(widths[i] - res.column_names[i].size(), ' ') << " ";
    }
    std::cout << "|" << std::endl;
    print_sep();

    for (const auto& row : res.rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            std::string s = row[i].to_string();
            std::cout << "| " << s 
                      << std::string(widths[i] - s.size(), ' ') << " ";
        }
        std::cout << "|" << std::endl;
    }
    print_sep();
    std::cout << res.message << std::endl;
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << " LuminaDB Interactive CLI Console" << std::endl;
    std::cout << " Version 1.0 (C++17 Volcano Storage Engine)" << std::endl;
    std::cout << " Type '.exit' or '.quit' to exit." << std::endl;
    std::cout << "===========================================" << std::endl;

    // Use current directory for temporary DB files
    std::string db_dir = ".";
    std::string db_name = "luminadb_cli_store";

    std::unique_ptr<luminadb::DB> db;
    try {
        db = luminadb::DB::open(db_dir, db_name);
        std::cout << "[LuminaDB] Database initialized successfully." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Error] Failed to initialize database: " << e.what() << std::endl;
        return 1;
    }

    std::string line;
    while (true) {
        std::cout << "lumina> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        // Trim helper
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), line.end());

        if (line.empty()) {
            continue;
        }

        if (line == ".exit" || line == ".quit") {
            std::cout << "[LuminaDB] Shutting down. Goodbye!" << std::endl;
            break;
        }

        auto start = std::chrono::high_resolution_clock::now();
        
        luminadb::QueryResult res = db->execute(line);
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = end - start;

        print_table(res);
        std::printf("Execution Time: %.2f ms\n\n", diff.count());
    }

    return 0;
}
