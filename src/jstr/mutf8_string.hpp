#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "libca/core/datatype.hpp"

/// @file mutf8_string.hpp
/// @brief Modified UTF-8（MUTF-8）字节的拥有/非拥有容器。
///
/// MUTF-8 是 JVM class 文件的字符串编码（NUL 为 C0 80，增补字符为代理对）。
/// 这两个类型只持有字节，不校验不转码；转码与校验见 conversion.hpp。
/// 除 std::string 分配失败外，本文件的方法不抛异常。

namespace jstr {

class MUtf8String;

/// @brief MUTF-8 字节序列的非拥有视图（指针 + 长度）。
///
/// 零拷贝借用外部字节（class 文件缓冲区、mmap 区域、字面量）。所有成员
/// noexcept。
///
/// @warning 视图不拥有字节，生命周期不得超过所指数据；字节被假定已是
///          MUTF-8，构造时不做校验。
class MUtf8StringRef
{
public:
    /// 构造空视图（data()==nullptr，size()==0）。
    MUtf8StringRef() noexcept = default;

    /// 视图化 @p data 处的 @p size 字节；字节生命周期由调用方保证。
    MUtf8StringRef(const char* data, ca::usize size) noexcept;

    /// 视图化 @p view 的字节；底层存储生命周期由调用方保证。
    explicit MUtf8StringRef(std::string_view view) noexcept;

    /// 视图化 NUL 结尾的 C 字符串；@p c_str 为空时返回空视图。
    static MUtf8StringRef from_c_str(const char* c_str) noexcept;

    const char* data() const noexcept { return data_; }
    ca::usize   size() const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

    const char* begin() const noexcept { return data_; }
    /// 默认构造的空视图 begin()==end()；空指针不做指针运算。
    const char* end() const noexcept { return data_ != nullptr ? data_ + size_ : data_; }

    /// 以 std::string_view 重新解释同一段字节（零拷贝）；空视图返回空 view。
    std::string_view as_std_view() const noexcept;

private:
    const char* data_ = nullptr;
    ca::usize   size_ = 0;
};

// ============================================================================
// MUtf8String — MUTF-8 字节的拥有容器
// ============================================================================

/// @brief MUTF-8 字节的拥有容器（底层 std::string）。
///
/// 持有独立副本，不校验不转码（见 conversion.hpp）。
///
/// @note 构造函数分配内存，可能抛 std::bad_alloc / std::length_error；
///       所有访问器 noexcept。
class MUtf8String
{
public:
    MUtf8String() = default;
    /// 接管 @p bytes（移动构造）。
    explicit MUtf8String(std::string bytes);
    /// 从字节向量拷贝原始字节。
    explicit MUtf8String(const std::vector<ca::u8>& bytes);

    const char* data() const noexcept;
    ca::usize   size() const noexcept;
    bool        empty() const noexcept;

    /// 返回非拥有视图；仅在本对象存活期间有效。
    MUtf8StringRef view() const noexcept;

    const std::string& bytes() const noexcept { return bytes_; }

private:
    std::string bytes_;
};

};   // namespace jstr
