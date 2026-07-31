#ifndef LUMINADB_TESTS_FRAMEWORK_H
#define LUMINADB_TESTS_FRAMEWORK_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <exception>

namespace luminadb {
namespace test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline void register_test(std::string suite, std::string name, std::function<void()> func) {
    get_tests().push_back({std::move(suite), std::move(name), std::move(func)});
}

#define TEST(suite, name) \
    void test_##suite##_##name(); \
    static int reg_##suite##_##name = []() { \
        luminadb::test::register_test(#suite, #name, test_##suite##_##name); \
        return 0; \
    }(); \
    void test_##suite##_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion Failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("Assertion Failed"); \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(val1, val2) \
    do { \
        if (!((val1) == (val2))) { \
            std::cerr << "Assertion Failed: " << #val1 << " == " << #val2 \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            throw std::runtime_error("Assertion Failed"); \
        } \
    } while(0)

} // namespace test
} // namespace luminadb

#endif // LUMINADB_TESTS_FRAMEWORK_H
