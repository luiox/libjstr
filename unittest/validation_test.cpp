#include <gtest/gtest.h>
#include <string>
#include <vector>
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

// 校验入口不做有损替换：kLenient 也执行严格校验（mode 仅为 API 对称保留）。
TEST(LibJstrValidateTest, ValidateUtf8LenientModeStillValidates)
{
    const std::string bad = std::string("A") + static_cast<char>(0xFF);
    auto              r   = validate_utf8(bad, ConversionValidationMode::kLenient);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
}

TEST(LibJstrValidateTest, ValidateMutf8LenientModeStillValidates)
{
    const std::string m("\xED\xA0\xBD", 3);   // 未配对高代理
    auto              r = validate_mutf8(MUtf8StringRef(m.data(), m.size()), ConversionValidationMode::kLenient);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

// C0 80 属 overlong 编码，标准 UTF-8 不允许：与 utf8_to_mutf8 strict 的拒绝结论一致。
TEST(LibJstrValidateTest, ValidateUtf8RejectsOverlongTwoByte)
{
    const std::string overlong("\xC0\x80", 2);
    auto              r = validate_utf8(overlong);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
}

// validate_utf8 与转换入口共用 strict 解码器：截断报 kUnexpectedEnd，
// offset 指向序列首字节（旧实现只返回 kInvalidUtf8Sequence + offset 0）。
TEST(LibJstrValidateTest, ValidateUtf8ReportsTruncationCodeAndOffset)
{
    const std::string trunc("A\xE4\xB8", 3);
    auto              r = validate_utf8(trunc);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kUnexpectedEnd);
    EXPECT_EQ(r.unwrap_err().offset, 1u);
}

TEST(LibJstrValidateTest, ValidateUtf8ReportsInvalidByteOffsetMidString)
{
    const std::string bad("AB\xFF", 3);
    auto              r = validate_utf8(bad);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
    EXPECT_EQ(r.unwrap_err().offset, 2u);
}

// 续字节位置非法：offset 指向首个非法字节，而不是序列首字节。
TEST(LibJstrValidateTest, ValidateUtf8ReportsBadContinuationOffset)
{
    const std::string bad("\xE4\x41\x42", 3);
    auto              r = validate_utf8(bad);
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
    EXPECT_EQ(r.unwrap_err().offset, 1u);
}

TEST(LibJstrValidateTest, ValidateMutf8ReportsBadContinuationOffset)
{
    const std::string bad("\xE4\x41\x42", 3);
    auto              r = validate_mutf8(MUtf8StringRef(bad.data(), bad.size()));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
    EXPECT_EQ(r.unwrap_err().offset, 1u);
}

// 孤立低位代理：strict 必须拒绝。
TEST(LibJstrValidateTest, ValidateMutf8RejectsOrphanLowSurrogate)
{
    const std::string m("\xED\xB8\x80", 3);
    auto              r = validate_mutf8(MUtf8StringRef(m.data(), m.size()));
    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
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

TEST(LibJstrMutf8StringTest, RefFromCStrNullGivesEmpty)
{
    MUtf8StringRef ref = MUtf8StringRef::from_c_str(nullptr);
    EXPECT_TRUE(ref.empty());
    EXPECT_EQ(ref.data(), nullptr);
}

// 从 std::vector<u8> 构造：拷贝字节。
TEST(LibJstrMutf8StringTest, ConstructFromByteVector)
{
    const std::vector<ca::u8> raw = {'h', 'i'};
    MUtf8String               s(raw);
    EXPECT_EQ(s.size(), 2u);
    EXPECT_EQ(s.bytes(), "hi");
}

TEST(LibJstrMutf8StringTest, DefaultRefIsEmpty)
{
    MUtf8StringRef ref;
    EXPECT_TRUE(ref.empty());
    EXPECT_EQ(ref.size(), 0u);
}

// 空视图的 data() 为空指针：end() 与 as_std_view() 不得做空指针运算。
TEST(LibJstrMutf8StringTest, DefaultRefEndAndStdViewAreSafe)
{
    MUtf8StringRef ref;
    EXPECT_EQ(ref.begin(), ref.end());
    EXPECT_TRUE(ref.as_std_view().empty());
}
