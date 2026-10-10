#include <gtest/gtest.h>
#include <string>
#include "jstr/conversion.hpp"

using namespace ca;
using namespace jstr;

// Direct coverage for the validate_utf8 / validate_mutf8 public API, which had
// no callers and no tests (conversion roundtrips are exercised elsewhere).

// ---- validate_utf8 ----

TEST(LibJstrValidateTest, ValidateUtf8AcceptsAsciiAndBmp)
{
    EXPECT_TRUE(validate_utf8(std::string("abc\xE4\xB8\xAD")).is_ok());
}

TEST(LibJstrValidateTest, ValidateUtf8AcceptsEmpty)
{
    EXPECT_TRUE(validate_utf8(std::string()).is_ok());
}

TEST(LibJstrValidateTest, ValidateUtf8RejectsInvalidLeadingByte)
{
    const std::string bad = std::string("A") + static_cast<char>(0xFF);
    auto              r   = validate_utf8(bad);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
}

TEST(LibJstrValidateTest, ValidateUtf8AcceptsSupplementaryFourByte)
{
    // U+1F600 emoji, valid 4-byte UTF-8.
    EXPECT_TRUE(validate_utf8(std::string("\xF0\x9F\x98\x80", 4)).is_ok());
}

// ---- validate_mutf8 ----

TEST(LibJstrValidateTest, ValidateMutf8AcceptsC0_80NulEncoding)
{
    const std::string m("A\xC0\x80"
                        "B",
                        4);
    EXPECT_TRUE(validate_mutf8(MUtf8StringRef(m.data(), m.size())).is_ok());
}

TEST(LibJstrValidateTest, ValidateMutf8AcceptsSurrogatePair)
{
    const std::string m("\xED\xA0\xBD\xED\xB8\x80", 6);
    EXPECT_TRUE(validate_mutf8(MUtf8StringRef(m.data(), m.size())).is_ok());
}

TEST(LibJstrValidateTest, ValidateMutf8RejectsRawNul)
{
    // A raw 0x00 byte is illegal in MUTF-8 (must be C0 80).
    const std::string m("a\0b", 3);
    auto              r = validate_mutf8(MUtf8StringRef(m.data(), m.size()));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

TEST(LibJstrValidateTest, ValidateMutf8RejectsUnpairedHighSurrogate)
{
    const std::string m("\xED\xA0\xBD", 3);
    auto              r = validate_mutf8(MUtf8StringRef(m.data(), m.size()));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

TEST(LibJstrValidateTest, ValidateMutf8RejectsNullData)
{
    auto r = validate_mutf8(MUtf8StringRef(nullptr, 1));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidInput);
}

// ---- MUtf8String / MUtf8StringRef basic accessors ----

TEST(LibJstrMutf8StringTest, OwnedStringExposesBytesViewAndSize)
{
    MUtf8String s(std::string("hello"));
    EXPECT_EQ(s.size(), 5u);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.bytes(), "hello");
    MUtf8StringRef ref = s.view();
    EXPECT_EQ(ref.size(), 5u);
    EXPECT_EQ(ref.as_std_view(), std::string_view("hello"));
}

TEST(LibJstrMutf8StringTest, RefFromCStr)
{
    MUtf8StringRef ref = MUtf8StringRef::from_c_str("abc");
    EXPECT_EQ(ref.size(), 3u);
    EXPECT_FALSE(ref.empty());
}

TEST(LibJstrMutf8StringTest, DefaultRefIsEmpty)
{
    MUtf8StringRef ref;
    EXPECT_TRUE(ref.empty());
    EXPECT_EQ(ref.size(), 0u);
}
