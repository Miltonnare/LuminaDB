#include "test_framework.h"
#include "luminadb/core/value.h"
#include "luminadb/core/tlv.h"

namespace luminadb {
namespace test {

TEST(TLVCoderTest, RoundTripInt) {
    Value val1(static_cast<int64_t>(42));
    Value val2(static_cast<int64_t>(-999999));

    auto encoded1 = TLVCoder::encode(val1);
    auto encoded2 = TLVCoder::encode(val2);

    size_t bytes_read = 0;
    Value decoded1 = TLVCoder::decode(encoded1.data(), encoded1.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded1.size());
    ASSERT_EQ(decoded1.as_int(), 42);

    bytes_read = 0;
    Value decoded2 = TLVCoder::decode(encoded2.data(), encoded2.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded2.size());
    ASSERT_EQ(decoded2.as_int(), -999999);
}

TEST(TLVCoderTest, RoundTripFloat) {
    Value val1(3.14159);
    Value val2(-0.000456);

    auto encoded1 = TLVCoder::encode(val1);
    auto encoded2 = TLVCoder::encode(val2);

    size_t bytes_read = 0;
    Value decoded1 = TLVCoder::decode(encoded1.data(), encoded1.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded1.size());
    ASSERT_TRUE(decoded1.as_float() == 3.14159);

    bytes_read = 0;
    Value decoded2 = TLVCoder::decode(encoded2.data(), encoded2.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded2.size());
    ASSERT_TRUE(decoded2.as_float() == -0.000456);
}

TEST(TLVCoderTest, RoundTripString) {
    Value val1(std::string("hello world"));
    Value val2(std::string(""));

    auto encoded1 = TLVCoder::encode(val1);
    auto encoded2 = TLVCoder::encode(val2);

    size_t bytes_read = 0;
    Value decoded1 = TLVCoder::decode(encoded1.data(), encoded1.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded1.size());
    ASSERT_EQ(decoded1.as_string(), "hello world");

    bytes_read = 0;
    Value decoded2 = TLVCoder::decode(encoded2.data(), encoded2.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded2.size());
    ASSERT_EQ(decoded2.as_string(), "");
}

TEST(TLVCoderTest, RoundTripBlob) {
    std::vector<uint8_t> payload = {1, 2, 3, 255, 128, 0};
    Value val1(payload);

    auto encoded1 = TLVCoder::encode(val1);

    size_t bytes_read = 0;
    Value decoded1 = TLVCoder::decode(encoded1.data(), encoded1.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded1.size());
    ASSERT_TRUE(decoded1.as_blob() == payload);
}

TEST(TLVCoderTest, RoundTripNull) {
    Value val1;

    auto encoded1 = TLVCoder::encode(val1);

    size_t bytes_read = 0;
    Value decoded1 = TLVCoder::decode(encoded1.data(), encoded1.size(), bytes_read);
    ASSERT_EQ(bytes_read, encoded1.size());
    ASSERT_TRUE(decoded1.is_null());
}

TEST(TLVCoderTest, DecodeBoundsChecking) {
    // Too short header
    std::vector<uint8_t> short_hdr = {1, 0, 0};
    size_t br = 0;
    bool caught = false;
    try {
        TLVCoder::decode(short_hdr.data(), short_hdr.size(), br);
    } catch (const DBException& e) {
        if (e.code() == StatusCode::ERR_CORRUPT_INPUT) {
            caught = true;
        }
    }
    ASSERT_TRUE(caught);

    // Truncated value content
    std::vector<uint8_t> trunc_val = {
        static_cast<uint8_t>(DataType::INT), 
        0, 0, 0, 8, // Length 8
        1, 2, 3 // Only 3 bytes of value
    };
    caught = false;
    try {
        TLVCoder::decode(trunc_val.data(), trunc_val.size(), br);
    } catch (const DBException& e) {
        if (e.code() == StatusCode::ERR_CORRUPT_INPUT) {
            caught = true;
        }
    }
    ASSERT_TRUE(caught);
}

} // namespace test
} // namespace luminadb
