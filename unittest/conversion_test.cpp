#include <gtest/gtest.h>
#include <string>
#include "jstr/conversion.hpp"

using namespace ca;
using namespace jstr;

TEST(LibJstrConversionTest, Utf8ToMutf8AndBackKeepBytesForBmpText)
{
    const std::string raw      = "abc\xE4\xB8\xAD";
    auto              to_mutf8 = utf8_to_mutf8(raw);
    ASSERT_TRUE(to_mutf8.is_ok());
    EXPECT_EQ(to_mutf8.unwrap().bytes(), raw);

    auto back = mutf8_to_utf8(to_mutf8.unwrap().view());
    ASSERT_TRUE(back.is_ok());
    EXPECT_EQ(back.unwrap(), raw);
}

TEST(LibJstrConversionTest, Utf8NulEncodesToC0_80InMutf8)
{
    const std::string utf8_with_nul("A\0B", 3);
    auto              encoded = utf8_to_mutf8(utf8_with_nul);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_EQ(encoded.unwrap().bytes(),
              std::string("A\xC0\x80"
                          "B",
                          4));
}

TEST(LibJstrConversionTest, Mutf8C0_80DecodesToUtf8Nul)
{
    const std::string mutf8_with_nul("A\xC0\x80"
                                     "B",
                                     4);
    auto decoded = mutf8_to_utf8(MUtf8StringRef(mutf8_with_nul.data(), mutf8_with_nul.size()));
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.unwrap(), std::string("A\0B", 3));
}

TEST(LibJstrConversionTest, SupplementaryCodePointRoundTripUsesSurrogatePairInMutf8)
{
    const std::string utf8_emoji("\xF0\x9F\x98\x80", 4);
    const std::string expected_mutf8("\xED\xA0\xBD\xED\xB8\x80", 6);

    auto encoded = utf8_to_mutf8(utf8_emoji);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_EQ(encoded.unwrap().bytes(), expected_mutf8);

    auto decoded = mutf8_to_utf8(MUtf8StringRef(expected_mutf8.data(), expected_mutf8.size()));
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.unwrap(), utf8_emoji);
}

TEST(LibJstrConversionTest, InvalidMutf8NullDataReturnsError)
{
    auto to_utf8 = mutf8_to_utf8(MUtf8StringRef(nullptr, 1));
    ASSERT_TRUE(to_utf8.is_err());
    EXPECT_EQ(to_utf8.unwrap_err().code, EncodingErrorCode::kInvalidInput);
}

TEST(LibJstrConversionTest, StrictRejectsInvalidUtf8LeadingByte)
{
    const std::string malformed = std::string("A") + static_cast<char>(0xFF);
    auto              res       = utf8_to_mutf8_strict(malformed);
    ASSERT_TRUE(res.is_err());
    EXPECT_EQ(res.unwrap_err().code, EncodingErrorCode::kInvalidUtf8Sequence);
}

TEST(LibJstrConversionTest, LenientLossyUtf8ToMutf8ReplacesInvalidLeadingByteWithUfffd)
{
    const std::string malformed = std::string("A") + static_cast<char>(0xFF);
    const std::string expected("A\xEF\xBF\xBD", 4);

    ConversionOptions opts;
    opts.mode                    = ConversionValidationMode::kLenient;
    opts.allow_lossy_replacement = true;
    auto converted               = utf8_to_mutf8(malformed, opts);
    ASSERT_TRUE(converted.is_ok());
    EXPECT_EQ(converted.unwrap().bytes(), expected);
}

TEST(LibJstrConversionTest, StrictRejectsUnpairedHighSurrogateInMutf8)
{
    const std::string unpaired_high("\xED\xA0\xBD", 3);
    auto res = mutf8_to_utf8_strict(MUtf8StringRef(unpaired_high.data(), unpaired_high.size()));
    ASSERT_TRUE(res.is_err());
    EXPECT_EQ(res.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

// 孤立续字节出现在 lead 位置：strict 模式必须拒绝。旧实现里 0x80..0xBF
// 落入 3 字节分支被当作 lead，静默吞掉后续两个任意字节并产出错码点。
TEST(LibJstrConversionTest, StrictRejectsIsolatedContinuationByteAsLead)
{
    const std::string alone("\x80", 1);
    auto              alone_res = mutf8_to_utf8_strict(MUtf8StringRef(alone.data(), alone.size()));
    ASSERT_TRUE(alone_res.is_err());
    EXPECT_EQ(alone_res.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);

    // 中流出现：A 之后是孤立 0xBF，随后 0x42 不应被吞
    const std::string mid("\x41\xBF\x42", 3);
    auto              mid_res = mutf8_to_utf8_strict(MUtf8StringRef(mid.data(), mid.size()));
    ASSERT_TRUE(mid_res.is_err());
    EXPECT_EQ(mid_res.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

// 宽容 + lossy：孤立续字节按 U+FFFD 替换，不再产出错码点。
TEST(LibJstrConversionTest, LenientLossyReplacesIsolatedContinuationByteWithUfffd)
{
    const std::string input("\x41\xBF\x42", 3);
    const std::string expected("A\xEF\xBF\xBD" "B", 5);

    ConversionOptions opts;
    opts.mode                    = ConversionValidationMode::kLenient;
    opts.allow_lossy_replacement = true;
    auto converted =
        mutf8_to_utf8(MUtf8StringRef(input.data(), input.size()), opts);
    ASSERT_TRUE(converted.is_ok());
    EXPECT_EQ(converted.unwrap(), expected);
}

TEST(LibJstrConversionTest, Wtf8CarrierPreservesUnpairedSurrogateCodeUnits)
{
    const std::string mutf8("A\xC0\x80\xED\xA7\x85Z", 7);
    const std::string expected_wtf8("A\0\xED\xA7\x85Z", 6);

    auto decoded = mutf8_to_wtf8(MUtf8StringRef(mutf8.data(), mutf8.size()));
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded.unwrap(), expected_wtf8);

    auto encoded = wtf8_to_mutf8(decoded.unwrap());
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_EQ(encoded.unwrap().bytes(), mutf8);
}

TEST(LibJstrConversionTest, LenientLossyMutf8ToUtf8ReplacesUnpairedHighSurrogateWithUfffd)
{
    const std::string unpaired_high("\xED\xA0\xBD", 3);
    const std::string expected("\xEF\xBF\xBD", 3);

    ConversionOptions opts;
    opts.mode                    = ConversionValidationMode::kLenient;
    opts.allow_lossy_replacement = true;
    auto converted =
        mutf8_to_utf8(MUtf8StringRef(unpaired_high.data(), unpaired_high.size()), opts);
    ASSERT_TRUE(converted.is_ok());
    EXPECT_EQ(converted.unwrap(), expected);
}

TEST(LibJstrConversionTest, LenientWithoutLossyStillRejectsMalformedMutf8)
{
    const std::string unpaired_high("\xED\xA0\xBD", 3);

    ConversionOptions opts;
    opts.mode                    = ConversionValidationMode::kLenient;
    opts.allow_lossy_replacement = false;
    auto converted =
        mutf8_to_utf8(MUtf8StringRef(unpaired_high.data(), unpaired_high.size()), opts);
    ASSERT_TRUE(converted.is_err());
    EXPECT_EQ(converted.unwrap_err().code, EncodingErrorCode::kInvalidMutf8Sequence);
}

TEST(LibJstrConversionTest, LenientLossyMutf8ToUtf8ReplacesRawNulWithUfffd)
{
    const std::string input("a\0b", 3);
    const std::string expected("a\xEF\xBF\xBD"
                               "b",
                               5);

    ConversionOptions opts;
    opts.mode                    = ConversionValidationMode::kLenient;
    opts.allow_lossy_replacement = true;
    auto res                     = mutf8_to_utf8(MUtf8StringRef(input.data(), input.size()), opts);
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap(), expected);
}
