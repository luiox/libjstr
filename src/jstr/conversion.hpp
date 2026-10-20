#pragma once

#include <string>

#include "libca/core/result.hpp"
#include "libca/core/datatype.hpp"
#include "mutf8_string.hpp"

/// @file conversion.hpp
/// @brief UTF-8 与 Modified UTF-8 的互转与校验。
///
/// JVM 在 class 文件里用 MUTF-8 存储字符串：NUL 编码为 C0 80，增补字符编码为
/// 6 字节代理对。本头文件是两个编码世界的边界。所有入口把畸形输入表示为
/// ca::Result 错误而不抛异常；内存分配失败仍按 C++ 常规语义抛
/// std::bad_alloc（超大输入可能抛 std::length_error）。

namespace jstr {

/// @brief 转码/校验的失败原因。
enum class EncodingErrorCode : ca::u32
{
    kInvalidInput         = 1,   ///< 空指针配非零长度等非法参数。
    kUnexpectedEnd        = 2,   ///< 多字节序列被截断。
    kInvalidUtf8Sequence  = 3,   ///< 非法标准 UTF-8。
    kInvalidMutf8Sequence = 4,   ///< 非法 MUTF-8（裸 NUL、坏代理等）。
};

/// @brief 校验/转换的宽严档位。
///
/// kStrict 对任何畸形序列报精确错误。kLenient 只在与
/// ConversionOptions::allow_lossy_replacement 搭配时才改变转换行为（畸形输入
/// 替换为 U+FFFD 后继续）；单独使用时仍拒绝畸形输入。validate_* 校验入口不做
/// 有损替换：两种模式都执行严格校验。
enum class ConversionValidationMode : ca::u8
{
    kStrict  = 0,
    kLenient = 1,
};

/// @brief 转码选项。
///
/// @note allow_lossy_replacement 只在 kLenient 模式下生效；kStrict 下被忽略，
///       畸形输入始终报错。kLenient 且置位时，一个畸形序列按 Unicode 建议的
///       maximal subpart 替换为单个 U+FFFD（截断序列整体算一个），而非逐字节
///       各产出一个替换符。
struct ConversionOptions
{
    ConversionValidationMode mode                    = ConversionValidationMode::kStrict;
    bool                     allow_lossy_replacement = false;
};

/// @brief ca::Result 失败时携带的错误详情。
struct EncodingError
{
    EncodingErrorCode code   = EncodingErrorCode::kInvalidInput;  ///< 错误类别。
    ca::usize         offset = 0;   ///< 出错字节偏移：序列级错误指向序列首字节，
                                    ///< 续字节错误指向首个非法字节，截断指向序列首字节。
    std::string       message;      ///< 简短英文描述（诊断/日志用，非稳定文案）。
};

// === MUTF-8 -> UTF-8 ===
// 把 class 文件字节解码为标准 UTF-8。默认 strict；畸形输入（截断、坏代理、
// 裸 NUL）以 EncodingError 返回，不抛异常。

ca::Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef    mutf8,
                                                     ConversionOptions options);
ca::Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& mutf8,
                                                     ConversionOptions  options);

/// 把 JVM MUTF-8 解码为 WTF-8 字节载体，保留未配对 UTF-16 代理码元而不拒绝。
/// 成对代理同样按两个 3 字节单元逐字保留，不合并为 4 字节 UTF-8；
/// wtf8_to_mutf8 是其逆变换。仅用于必须原样保留 JVM String 码元的字节码常量，
/// 普通文本请用 mutf8_to_utf8。
ca::Result<std::string, EncodingError> mutf8_to_wtf8(MUtf8StringRef mutf8);
ca::Result<std::string, EncodingError> mutf8_to_wtf8(const MUtf8String& mutf8);

// === UTF-8 -> MUTF-8 ===
// 把标准 UTF-8 编码为 class 文件字节。默认 strict；畸形输入以 EncodingError
// 返回，不抛异常。

ca::Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& utf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& utf8,
                                                     ConversionOptions  options);

/// 把 WTF-8 字节载体编码回 JVM MUTF-8，保留未配对代理码元；是 mutf8_to_wtf8
/// 的逆变换。
ca::Result<MUtf8String, EncodingError> wtf8_to_mutf8(const std::string& wtf8);

// === Latin-1 -> MUTF-8 ===
// 把每字节（0-255）当独立 Latin-1 码点编码为 MUTF-8。与 utf8_to_mutf8 不同，
// 不按 UTF-8 解码，任意字节内容都可编码；唯一错误是 data 为空且 size 非零
// （内存分配失败按 C++ 常规语义抛异常）。用于 JVM 字节码混淆密文等字节流载体：
// 这类数据不是合法 UTF-8，utf8_to_mutf8 会拒绝，但每字节作为 Latin-1 字符在
// MUTF-8 下可 1:1 往返。
ca::Result<MUtf8String, EncodingError> latin1_to_mutf8(const ca::u8* data, ca::usize size);

// === 校验 ===
// 只检查合法性、不产出结果。校验入口不做有损替换：@p mode 两种取值都执行严格
// 校验，参数仅为与转换入口 API 对称而保留。

ca::Result<void, EncodingError> validate_utf8(const std::string& utf8);
ca::Result<void, EncodingError> validate_utf8(const std::string&       utf8,
                                              ConversionValidationMode mode);
ca::Result<void, EncodingError> validate_mutf8(MUtf8StringRef mutf8);
ca::Result<void, EncodingError> validate_mutf8(MUtf8StringRef mutf8, ConversionValidationMode mode);

// === strict/lenient 便捷入口 ===
// 对带 ConversionOptions 的重载的封装。_lenient 变体蕴含
// allow_lossy_replacement=true（畸形序列替换为 U+FFFD）。

ca::Result<std::string, EncodingError> mutf8_to_utf8_strict(MUtf8StringRef mutf8);
ca::Result<std::string, EncodingError> mutf8_to_utf8_lenient(MUtf8StringRef mutf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8_strict(const std::string& utf8);
ca::Result<MUtf8String, EncodingError> utf8_to_mutf8_lenient(const std::string& utf8);

}   // namespace jstr
