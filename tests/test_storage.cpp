#include "test_framework.h"
#include "luminadb/storage/page.h"
#include "luminadb/storage/disk_manager.h"
#include "luminadb/storage/table_heap.h"
#include "luminadb/storage/btree_index.h"
#include "luminadb/storage/replacer.h"
#include <cstdio>

namespace luminadb {
namespace test {

TEST(PageTest, SlottedPageInsertAndGet) {
    uint8_t page_buf[PAGE_SIZE];
    Page::init(page_buf);

    ASSERT_EQ(Page::get_slot_count(page_buf), 0);
    ASSERT_EQ(Page::get_free_space_pointer(page_buf), PAGE_SIZE);

    std::vector<uint8_t> rec1 = {1, 2, 3, 4};
    std::vector<uint8_t> rec2 = {10, 20, 30};

    int s1 = Page::insert_record(page_buf, rec1.data(), rec1.size());
    int s2 = Page::insert_record(page_buf, rec2.data(), rec2.size());

    ASSERT_EQ(s1, 0);
    ASSERT_EQ(s2, 1);
    ASSERT_EQ(Page::get_slot_count(page_buf), 2);

    const uint8_t* ptr = nullptr;
    size_t len = 0;
    
    ASSERT_TRUE(Page::get_record(page_buf, s1, ptr, len));
    ASSERT_EQ(len, rec1.size());
    ASSERT_TRUE(std::memcmp(ptr, rec1.data(), len) == 0);

    ASSERT_TRUE(Page::get_record(page_buf, s2, ptr, len));
    ASSERT_EQ(len, rec2.size());
    ASSERT_TRUE(std::memcmp(ptr, rec2.data(), len) == 0);
}

TEST(PageTest, SlottedPageDefragment) {
    uint8_t page_buf[PAGE_SIZE];
    Page::init(page_buf);

    std::vector<uint8_t> rec = {5, 5, 5, 5, 5};
    int s0 = Page::insert_record(page_buf, rec.data(), rec.size());
    int s1 = Page::insert_record(page_buf, rec.data(), rec.size());
    int s2 = Page::insert_record(page_buf, rec.data(), rec.size());

    // Delete middle record
    ASSERT_TRUE(Page::delete_record(page_buf, s1));

    const uint8_t* dummy = nullptr;
    size_t dummy_len = 0;
    ASSERT_FALSE(Page::get_record(page_buf, s1, dummy, dummy_len));

    // Defragment should compact and preserve s0 and s2
    Page::defragment(page_buf);

    ASSERT_TRUE(Page::get_record(page_buf, s0, dummy, dummy_len));
    ASSERT_TRUE(Page::get_record(page_buf, s2, dummy, dummy_len));
    ASSERT_FALSE(Page::get_record(page_buf, s1, dummy, dummy_len));
}

TEST(DiskManagerTest, ReadWritePage) {
    std::string filename = "test_disk_dm.db";
    std::remove(filename.c_str());

    {
        DiskManager dm(filename);
        page_id_t p0 = dm.allocate_page();
        page_id_t p1 = dm.allocate_page();
        ASSERT_EQ(p0, 0);
        ASSERT_EQ(p1, 1);

        uint8_t wbuf[PAGE_SIZE];
        std::memset(wbuf, 'A', PAGE_SIZE);
        dm.write_page(p0, wbuf);

        std::memset(wbuf, 'B', PAGE_SIZE);
        dm.write_page(p1, wbuf);
    } // Closes file

    {
        DiskManager dm(filename);
        ASSERT_EQ(dm.num_pages(), 2);
        
        uint8_t rbuf[PAGE_SIZE];
        dm.read_page(0, rbuf);
        ASSERT_EQ(rbuf[0], 'A');
        ASSERT_EQ(rbuf[PAGE_SIZE - 1], 'A');

        dm.read_page(1, rbuf);
        ASSERT_EQ(rbuf[0], 'B');
        ASSERT_EQ(rbuf[PAGE_SIZE - 1], 'B');
    }

    std::remove(filename.c_str());
}

TEST(BTreeIndexTest, BTreeInsertFind) {
    BTreeIndex idx;
    
    // Insert keys
    idx.insert(Value(10LL), 1001);
    idx.insert(Value(20LL), 1002);
    idx.insert(Value(5LL), 1003);
    idx.insert(Value(15LL), 1004);

    record_id_t val;
    ASSERT_TRUE(idx.find(Value(10LL), val));
    ASSERT_EQ(val, 1001);

    ASSERT_TRUE(idx.find(Value(5LL), val));
    ASSERT_EQ(val, 1003);

    ASSERT_FALSE(idx.find(Value(99LL), val));

    // Remove key
    idx.remove(Value(10LL));
    ASSERT_FALSE(idx.find(Value(10LL), val));
}

TEST(BTreeIndexTest, BTreeRangeScan) {
    BTreeIndex idx;
    idx.insert(Value(1LL), 10);
    idx.insert(Value(2LL), 20);
    idx.insert(Value(3LL), 30);
    idx.insert(Value(4LL), 40);
    idx.insert(Value(5LL), 50);

    auto res = idx.range_scan(Value(2LL), Value(4LL));
    ASSERT_EQ(res.size(), 3);
    ASSERT_EQ(res[0], 20);
    ASSERT_EQ(res[1], 30);
    ASSERT_EQ(res[2], 40);
}

TEST(ReplacerTest, ClockReplacerFlow) {
    ClockReplacer replacer(3);
    
    replacer.unpin(0);
    replacer.unpin(1);
    replacer.unpin(2);
    ASSERT_EQ(replacer.size(), 3);
    
    frame_id_t f;
    ASSERT_TRUE(replacer.victim(f));
    ASSERT_EQ(f, 0);
    
    replacer.pin(1);
    ASSERT_EQ(replacer.size(), 1);
    
    ASSERT_TRUE(replacer.victim(f));
    ASSERT_EQ(f, 2);
}

TEST(ReplacerTest, LfuReplacerFlow) {
    LfuReplacer replacer(3);
    
    replacer.unpin(10);
    replacer.unpin(20);
    replacer.unpin(30);
    
    replacer.unpin(20);
    replacer.unpin(30);
    replacer.unpin(30);
    
    frame_id_t f;
    ASSERT_TRUE(replacer.victim(f));
    ASSERT_EQ(f, 10);
}

} // namespace test
} // namespace luminadb
