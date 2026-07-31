#include "test_framework.h"
#include <iostream>

int main() {
    auto& tests = luminadb::test::get_tests();
    std::cout << "[LuminaDB Tests] Running " << tests.size() << " tests..." << std::endl;

    int passed = 0;
    int failed = 0;

    for (const auto& t : tests) {
        std::cout << "[ RUN      ] " << t.suite << "." << t.name << std::endl;
        try {
            t.func();
            std::cout << "[       OK ] " << t.suite << "." << t.name << std::endl;
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "[  FAILED  ] " << t.suite << "." << t.name << " (Exception: " << e.what() << ")" << std::endl;
            failed++;
        } catch (...) {
            std::cerr << "[  FAILED  ] " << t.suite << "." << t.name << " (Unknown Exception)" << std::endl;
            failed++;
        }
    }

    std::cout << "[==========] " << passed + failed << " tests ran." << std::endl;
    std::cout << "[  PASSED  ] " << passed << " tests." << std::endl;
    if (failed > 0) {
        std::cout << "[  FAILED  ] " << failed << " tests." << std::endl;
        return 1;
    }
    return 0;
}
