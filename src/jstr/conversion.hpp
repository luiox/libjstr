#pragma once

#include <string>

#include "libca/core/result.hpp"
#include "libca/core/datatype.hpp"
#include "mutf8_string.hpp"

/// @file conversion.hpp
/// @brief UTF-8 <-> Modified UTF-8 transcoding and validation.
///
/// The JVM stores class-file strings as MUTF-8 (NUL encoded as C0 80,
/// supplementary code points as 6-byte surrogate pairs). This header is the
/// boundary between MUTF-8 (class-file side) and standard UTF-8 (everything
/// else). All entry points report failure via ca::Result and never throw — even
/// on malformed input, an error is returned, not raised.

namespace jstr {

/// @brief Failure reasons for transcoding/validation.
enum class EncodingErrorCode : ca::u32
{
    kInvalidInput         = 1,   ///< Null data with non-zero length, etc.
    kUnexpectedEnd        = 2,   ///< Truncated multi-byte sequence.
    kInvalidUtf8Sequence  = 3,   ///< Ill-formed standard UTF-8.
    kInvalidMutf8Sequence = 4,   ///< Ill-formed MUTF-8 (raw NUL, bad surrogate…).
    kUnsupportedCodePoint = 5,   ///< Code point not representable in the target.
};

/// @brief Validation strictness.
///
/// kStrict rejects any ill-formed sequence with a precise error. kLenient only
/// has an effect together with ConversionOptions::allow_lossy_replacement.
enum class ConversionValidationMode : ca::u8
{
    kStrict  = 0,
    kLenient = 1,
};

/// @brief Transcoding options.
///
/// @note allow_lossy_replacement only applies in kLenient mode; in kStrict mode
///       it is ignored and malformed input always errors. In kLenient mode with
///       this flag set, malformed sequences become U+FFFD instead of failing;
///       with it unset, kLenient still rejects malformed input.
struct ConversionOptions
{
    ConversionValidationMode mode                    = ConversionValidationMode::kStrict;
    bool                     allow_lossy_replacement = false;
};

/// @brief Error detail returned inside ca::Result on failure.
struct EncodingError
{
    EncodingErrorCode code   = EncodingErrorCode::kInvalidInput;
    ca::usize         offset = 0;   ///< Byte offset where the error was detected.
    std::string       message;
};

// === MUTF-8 -> UTF-8 ===
// Decode class-file bytes to standard UTF-8. Default mode is strict. None throw;
// errors (truncation, bad surrogates, raw NUL) come back as EncodingError.

ca::Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef    mutf8,
                                                     ConversionOptions options);
ca::Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& mutf8,
                                                     ConversionOptions  options);

/// Decode JVM MUTF-8 into a WTF-8 byte carrier without rejecting unpaired UTF-16
/// surrogate code units. This is only for bytecode constants that must preserve
/// their original JVM String code units; ordinary text should use mutf8_to_utf8.
ca::Result<std::string, EncodingError> mutf8_to_wtf8(MUtf8StringRef mutf8);

// === UTF-8 -> MUTF-8 ===
// Encode standard UTF-8 to class-file bytes. Default mode is strict.

ca::Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& utf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& utf8,
                                                     ConversionOptions  options);

/// Encode a WTF-8 byte carrier back to JVM MUTF-8 while preserving unpaired
/// surrogate code units. This is the inverse of mutf8_to_wtf8.
ca::Result<MUtf8String, EncodingError> wtf8_to_mutf8(const std::string& wtf8);

// === Latin-1 -> MUTF-8 ===
// 把每字节（0-255）当独立 Latin-1 code point 编码为 modified UTF-8。
// 与 utf8_to_mutf8 不同：不把输入当 UTF-8 解码，每字节独立处理，**永不失败**。
// 用于字节流载体（JVM 字节码混淆的密文等）：这类数据不是合法 UTF-8，
// utf8_to_mutf8 会拒绝，但每字节作为 Latin-1 char 在 JVM modified UTF-8 下可 1:1 往返。
ca::Result<MUtf8String, EncodingError> latin1_to_mutf8(const ca::u8* data, ca::usize size);

// === Validation ===
// Check well-formedness without producing output. void-result is Ok when valid.

ca::Result<void, EncodingError> validate_utf8(const std::string& utf8);
ca::Result<void, EncodingError> validate_utf8(const std::string&       utf8,
                                              ConversionValidationMode mode);
ca::Result<void, EncodingError> validate_mutf8(MUtf8StringRef mutf8);
ca::Result<void, EncodingError> validate_mutf8(MUtf8StringRef mutf8, ConversionValidationMode mode);

// === Strict/Lenient shortcuts ===
// Convenience wrappers over the options-taking overloads. The _lenient variants
// imply allow_lossy_replacement=true (malformed -> U+FFFD).

ca::Result<std::string, EncodingError> mutf8_to_utf8_strict(MUtf8StringRef mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8_lenient(MUtf8StringRef mutf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8_strict(const std::string& utf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8_lenient(const std::string& utf8);

}   // namespace jstr
