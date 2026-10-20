#include "conversion.hpp"
#include "libca/str/utf8_util.hpp"
#include <string>

using namespace ca;

namespace jstr {
namespace {

constexpr u32 kReplacementCodePoint = 0xFFFDu;

// ===== helpers =====

EncodingError make_error(EncodingErrorCode code, usize offset, const char* msg)
{
    EncodingError e;
    e.code    = code;
    e.offset  = offset;
    e.message = msg;
    return e;
}

Result<void, EncodingError> validate_common(const char* d, usize sz, const char* msg)
{
    if (d == nullptr && sz != 0) return Err(make_error(EncodingErrorCode::kInvalidInput, 0, msg));
    return Ok();
}

bool is_high_surrogate(u16 v)
{
    return v >= 0xD800u && v <= 0xDBFFu;
}
bool is_low_surrogate(u16 v)
{
    return v >= 0xDC00u && v <= 0xDFFFu;
}
bool is_continuation_byte(u8 b)
{
    return (b & 0xC0u) == 0x80u;
}

// ===== UTF-8 encode (delegate to ca::str) =====

bool append_utf8_code_point(std::string& out, u32 cp)
{
    if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
    if (cp > 0x10FFFFu) return false;
    u8    buf[4];
    usize n = ca::str::utf8_encode_code_point(cp, buf);
    if (n == 0) return false;
    out.append((const char*)buf, n);
    return true;
}

void append_wtf8_unit(std::string& out, u16 unit)
{
    if (unit <= 0x7Fu) {
        out.push_back(static_cast<char>(unit));
    }
    else if (unit <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (unit >> 6)));
        out.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
    }
    else {
        out.push_back(static_cast<char>(0xE0u | (unit >> 12)));
        out.push_back(static_cast<char>(0x80u | ((unit >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (unit & 0x3Fu)));
    }
}

void append_replacement(std::string& out)
{
    u8    buf[4];
    usize n = ca::str::utf8_encode_code_point(0xFFFDu, buf);
    out.append((const char*)buf, n);
}

// ===== MUTF-8 encode =====

void append_mutf8_unit(std::string& out, u16 cu)
{
    if (cu == 0) {
        out.push_back((char)0xC0u);
        out.push_back((char)0x80u);
        return;
    }
    if (cu <= 0x7Fu) {
        out.push_back((char)cu);
        return;
    }
    if (cu <= 0x7FFu) {
        out.push_back((char)(0xC0u | (cu >> 6)));
        out.push_back((char)(0x80u | (cu & 0x3Fu)));
        return;
    }
    out.push_back((char)(0xE0u | (cu >> 12)));
    out.push_back((char)(0x80u | ((cu >> 6) & 0x3Fu)));
    out.push_back((char)(0x80u | (cu & 0x3Fu)));
}

// 前提：cp 是合法 Unicode 标量值（非代理、<= U+10FFFF）。strict 解码器已经
// 拒绝代理与越界码点，此函数不再重复判定。
void append_mutf8_cp(std::string& out, u32 cp)
{
    if (cp <= 0xFFFFu) {
        append_mutf8_unit(out, (u16)cp);
        return;
    }
    u32 v = cp - 0x10000u;
    append_mutf8_unit(out, (u16)(0xD800u + (v >> 10)));
    append_mutf8_unit(out, (u16)(0xDC00u + (v & 0x3FFu)));
}

// ===== UTF-8 decode with error reporting =====

Result<u32, EncodingError> decode_utf8_strict(const char* d, usize sz, usize& off)
{
    usize start = off;
    u8    b0    = (u8)d[off];
    usize clen  = ca::str::utf8_code_point_bytes(b0);
    if (clen == 0)
        return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad lead"));
    if (clen == 1) {
        ++off;
        return Ok((u32)b0);
    }
    // 先逐个检查已存在的续字节：非法字节优先于截断报告，offset 落在具体字节上。
    for (usize i = 1; i < clen; ++i) {
        if (off + i >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        if (!is_continuation_byte((u8)d[off + i]))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + i, "bad cont"));
    }
    if (clen == 2) {
        // C0/C1 lead 解出 cp < 0x80，属 overlong 编码，标准 UTF-8 不允许
        if (b0 < 0xC2u)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "overlong"));
        u8 b1 = (u8)d[start + 1];
        off += 2;
        return Ok((u32)(((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu)));
    }
    u8 b1 = (u8)d[start + 1];
    if (clen == 3) {
        u8 b2 = (u8)d[start + 2];
        if (b0 == 0xE0u && b1 < 0xA0u)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "overlong"));
        if (b0 == 0xEDu && b1 > 0x9Fu)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "surrogate"));
        off += 3;
        return Ok((u32)(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)));
    }
    u8 b2 = (u8)d[start + 2], b3 = (u8)d[start + 3];
    // F5-F7 也会被判为 4 字节 lead，但解出 U+10FFFF 之上的码点，同属非法 UTF-8
    if (b0 > 0xF4u || (b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu))
        return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "range"));
    off += 4;
    return Ok(
        (u32)(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu)));
}

// ===== MUTF-8 decode =====

Result<u16, EncodingError> decode_mutf8_strict(const char* d, usize sz, usize& off)
{
    usize start = off;
    u8    b0    = (u8)d[off];
    if (b0 == 0) return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "raw NUL"));
    if (b0 <= 0x7Fu) {
        ++off;
        return Ok((u16)b0);
    }
    if (b0 == 0xC0u) {
        if (off + 1 >= sz)
            return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        if ((u8)d[off + 1] != 0x80u)
            return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "bad C0"));
        off += 2;
        return Ok((u16)0);
    }
    if (b0 == 0xC1u || b0 >= 0xF0u)
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "bad lead"));
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (off + 1 >= sz)
            return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = (u8)d[off + 1];
        if (!is_continuation_byte(b1))
            return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start + 1, "bad cont"));
        off += 2;
        return Ok((u16)(((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu)));
    }
    // 0x80..0xBF 落到这里：孤立续字节出现在 lead 位置，strict 模式必须拒绝
    // （否则被当作 3 字节 lead 吞掉后续两个任意字节，静默产出错码点）。
    if (b0 < 0xE0u)
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "bad lead"));
    if (off + 1 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
    u8 b1 = (u8)d[off + 1];
    if (!is_continuation_byte(b1))
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start + 1, "bad cont"));
    if (b0 == 0xE0u && b1 < 0xA0u)
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "overlong"));
    if (off + 2 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
    u8 b2 = (u8)d[off + 2];
    if (!is_continuation_byte(b2))
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start + 2, "bad cont"));
    off += 3;
    return Ok((u16)(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)));
}

Result<u32, EncodingError> decode_wtf8_code_point(const char* d, usize sz, usize& off)
{
    usize start = off;
    u8    b0    = static_cast<u8>(d[off]);
    if (b0 <= 0x7Fu) {
        ++off;
        return Ok(static_cast<u32>(b0));
    }
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (off + 1 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = static_cast<u8>(d[off + 1]);
        if (!is_continuation_byte(b1))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 1, "bad cont"));
        off += 2;
        return Ok(static_cast<u32>(((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu)));
    }
    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        if (off + 1 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = static_cast<u8>(d[off + 1]);
        if (!is_continuation_byte(b1))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 1, "bad cont"));
        if (b0 == 0xE0u && b1 < 0xA0u)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "overlong"));
        if (off + 2 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b2 = static_cast<u8>(d[off + 2]);
        if (!is_continuation_byte(b2))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 2, "bad cont"));
        off += 3;
        return Ok(static_cast<u32>(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)));
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (off + 1 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = static_cast<u8>(d[off + 1]);
        if (!is_continuation_byte(b1))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 1, "bad cont"));
        if ((b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "range"));
        if (off + 2 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b2 = static_cast<u8>(d[off + 2]);
        if (!is_continuation_byte(b2))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 2, "bad cont"));
        if (off + 3 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b3 = static_cast<u8>(d[off + 3]);
        if (!is_continuation_byte(b3))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 3, "bad cont"));
        off += 4;
        return Ok(static_cast<u32>(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
                                   ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu)));
    }
    return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad lead"));
}

// ===== lossy replacement granularity =====

// 有损替换按 Unicode 建议的 maximal subpart 吞字节：一个畸形序列只产出一个
// U+FFFD，而不是逐字节产出多个。返回值 = 合法前缀 + 至多一个非法字节；
// 截断序列（kUnexpectedEnd）吞到串尾。
usize utf8_error_subpart_length(const char* d, usize sz, usize start, EncodingErrorCode code)
{
    if (code == EncodingErrorCode::kUnexpectedEnd) return sz - start;
    u8    b0   = (u8)d[start];
    usize clen = ca::str::utf8_code_point_bytes(b0);
    if (clen <= 1) return 1;
    if (clen == 2 && b0 < 0xC2u) return 1;   // C0/C1 overlong lead 按单字节非法处理
    if (start + 1 >= sz) return 1;
    u8   b1  = (u8)d[start + 1];
    bool ok1 = is_continuation_byte(b1);
    if (clen == 3) {
        if (b0 == 0xE0u) ok1 = b1 >= 0xA0u && b1 <= 0xBFu;
        else if (b0 == 0xEDu) ok1 = b1 >= 0x80u && b1 <= 0x9Fu;
    }
    else if (clen == 4) {
        if (b0 == 0xF0u) ok1 = b1 >= 0x90u && b1 <= 0xBFu;
        else if (b0 == 0xF4u) ok1 = b1 >= 0x80u && b1 <= 0x8Fu;
        else if (b0 > 0xF4u) ok1 = false;
    }
    if (!ok1) return 1;
    usize len = 2;
    while (len < clen && start + len < sz && is_continuation_byte((u8)d[start + len])) ++len;
    return len;
}

usize mutf8_error_subpart_length(const char* d, usize sz, usize start, EncodingErrorCode code)
{
    if (code == EncodingErrorCode::kUnexpectedEnd) return sz - start;
    u8 b0 = (u8)d[start];
    if (b0 < 0xC2u || b0 >= 0xF0u) return 1;   // 裸 NUL / C0 非法组合 / C1 / 非法 lead
    if (b0 <= 0xDFu) return 1;                 // 2 字节分支出错只可能是续字节非法
    if (start + 1 >= sz) return 1;
    u8 b1 = (u8)d[start + 1];
    if (!is_continuation_byte(b1) || (b0 == 0xE0u && b1 < 0xA0u)) return 1;
    return 2;   // b1 合法，错误在 b2（缺失或非法）
}

// ===== validation =====

Result<void, EncodingError> validate_utf8_strict_bytes(const char* d, usize sz)
{
    usize off = 0;
    while (off < sz) {
        auto r = decode_utf8_strict(d, sz, off);
        if (r.is_err()) return Err(r.unwrap_err());
    }
    return Ok();
}

Result<void, EncodingError> validate_mutf8_strict_bytes(const char* d, usize sz)
{
    bool  wl = false;
    usize ho = 0, off = 0;
    while (off < sz) {
        usize st = off;
        auto  r  = decode_mutf8_strict(d, sz, off);
        if (r.is_err()) return Err(r.unwrap_err());
        u16 u = r.unwrap();
        if (wl) {
            if (!is_low_surrogate(u))
                return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, ho, "no low"));
            wl = false;
            continue;
        }
        if (is_high_surrogate(u)) {
            wl = true;
            ho = st;
            continue;
        }
        if (is_low_surrogate(u))
            return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, st, "orphan low"));
    }
    if (wl) return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, ho, "no low"));
    return Ok();
}

// ===== mutf8 -> utf8 impl =====

Result<std::string, EncodingError> mutf8_to_utf8_impl(MUtf8StringRef m, ConversionOptions opts)
{
    auto c = validate_common(m.data(), m.size(), "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (m.empty()) return Ok(std::string());
    std::string u;
    u.reserve(m.size());
    bool  wl  = false;
    u16   ph  = 0;
    usize pho = 0, off = 0;
    while (off < m.size()) {
        usize st = off;
        auto  r  = decode_mutf8_strict(m.data(), m.size(), off);
        if (r.is_err()) {
            if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement) {
                auto err = r.unwrap_err();
                if (wl) {
                    append_replacement(u);
                    wl = false;
                }
                append_replacement(u);
                off = st + mutf8_error_subpart_length(m.data(), m.size(), st, err.code);
                continue;
            }
            return Err(r.unwrap_err());
        }
        u16 un = r.unwrap();
        if (wl) {
            if (is_low_surrogate(un)) {
                u32 cp = 0x10000u + (((u32)(ph - 0xD800u) << 10) | (u32)(un - 0xDC00u));
                append_utf8_code_point(u, cp);
                wl = false;
                continue;
            }
            if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement) {
                append_replacement(u);
                wl = false;
            }
            else {
                return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, pho, "no low"));
            }
        }
        if (is_high_surrogate(un)) {
            wl  = true;
            ph  = un;
            pho = st;
            continue;
        }
        if (is_low_surrogate(un)) {
            if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement) {
                append_replacement(u);
                continue;
            }
            return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, st, "orphan low"));
        }
        append_utf8_code_point(u, (u32)un);
    }
    if (wl) {
        if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement)
            append_replacement(u);
        else
            return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, pho, "no low"));
    }
    return Ok(std::move(u));
}

// ===== utf8 -> mutf8 impl =====

Result<MUtf8String, EncodingError> utf8_to_mutf8_impl(const char* data, usize size,
                                                      ConversionOptions opts)
{
    auto c = validate_common(data, size, "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (size == 0) return Ok(MUtf8String());
    std::string m;
    m.reserve(size * 2);
    usize off = 0;
    while (off < size) {
        usize st = off;
        auto  r  = decode_utf8_strict(data, size, off);
        if (r.is_err()) {
            if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement) {
                auto err = r.unwrap_err();
                append_mutf8_cp(m, kReplacementCodePoint);
                off = st + utf8_error_subpart_length(data, size, st, err.code);
                continue;
            }
            return Err(r.unwrap_err());
        }
        // strict UTF-8 解码器只产出合法标量值，append_mutf8_cp 不会再失败
        append_mutf8_cp(m, r.unwrap());
    }
    return Ok(MUtf8String(std::move(m)));
}

}   // anonymous namespace

// ===== public API =====

Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef m)
{
    return mutf8_to_utf8_impl(m, ConversionOptions{});
}
Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& m)
{
    return mutf8_to_utf8_impl(m.view(), ConversionOptions{});
}

Result<std::string, EncodingError> mutf8_to_utf8(MUtf8StringRef m, ConversionOptions opts)
{
    return mutf8_to_utf8_impl(m, opts);
}

Result<std::string, EncodingError> mutf8_to_utf8(const MUtf8String& m, ConversionOptions opts)
{
    return mutf8_to_utf8(m.view(), opts);
}

Result<std::string, EncodingError> mutf8_to_wtf8(MUtf8StringRef m)
{
    auto valid = validate_common(m.data(), m.size(), "null");
    if (valid.is_err()) return Err(valid.unwrap_err());

    std::string result;
    result.reserve(m.size());
    usize offset = 0;
    while (offset < m.size()) {
        auto unit = decode_mutf8_strict(m.data(), m.size(), offset);
        if (unit.is_err()) return Err(unit.unwrap_err());
        append_wtf8_unit(result, unit.unwrap());
    }
    return Ok(std::move(result));
}

Result<std::string, EncodingError> mutf8_to_wtf8(const MUtf8String& m)
{
    return mutf8_to_wtf8(m.view());
}

// ===== MUTF-8 <-> UTF-16 code units =====

Result<std::u16string, EncodingError> mutf8_to_utf16(MUtf8StringRef m, Mutf8ToUtf16Options opts)
{
    auto valid = validate_common(m.data(), m.size(), "null");
    if (valid.is_err()) return Err(valid.unwrap_err());

    std::u16string result;
    result.reserve(m.size());
    usize offset = 0;
    while (offset < m.size()) {
        const u8 b0 = static_cast<u8>(m.data()[offset]);
        if (b0 == 0 && opts.allow_raw_nul) {
            ++offset;
            result.push_back(0);
            continue;
        }
        if (b0 >= 0xF0u && opts.allow_standard_utf8_four_byte) {
            auto cp = decode_wtf8_code_point(m.data(), m.size(), offset);
            if (cp.is_err()) return Err(cp.unwrap_err());
            const u32 v = cp.unwrap() - 0x10000u;
            result.push_back(static_cast<u16>(0xD800u + (v >> 10)));
            result.push_back(static_cast<u16>(0xDC00u + (v & 0x3FFu)));
            continue;
        }
        auto unit = decode_mutf8_strict(m.data(), m.size(), offset);
        if (unit.is_err()) return Err(unit.unwrap_err());
        result.push_back(unit.unwrap());
    }
    return Ok(std::move(result));
}

Result<std::u16string, EncodingError> mutf8_to_utf16(MUtf8StringRef m)
{
    return mutf8_to_utf16(m, Mutf8ToUtf16Options{});
}

Result<std::u16string, EncodingError> mutf8_to_utf16(const MUtf8String& m)
{
    return mutf8_to_utf16(m.view());
}

Result<std::u16string, EncodingError> mutf8_to_utf16(const MUtf8String& m, Mutf8ToUtf16Options opts)
{
    return mutf8_to_utf16(m.view(), opts);
}

Result<MUtf8String, EncodingError> utf16_to_mutf8(std::u16string_view u)
{
    if (u.data() == nullptr && !u.empty()) {
        return Err(make_error(EncodingErrorCode::kInvalidInput, 0, "null"));
    }
    std::string result;
    result.reserve(u.size());
    for (const u16 unit : u) append_mutf8_unit(result, unit);
    return Ok(MUtf8String(std::move(result)));
}

Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& u)
{
    return utf8_to_mutf8(u, ConversionOptions{});
}

Result<MUtf8String, EncodingError> utf8_to_mutf8(const std::string& u, ConversionOptions opts)
{
    return utf8_to_mutf8_impl(u.data(), u.size(), opts);
}

Result<MUtf8String, EncodingError> wtf8_to_mutf8(const std::string& wtf8)
{
    std::string result;
    result.reserve(wtf8.size());
    usize offset = 0;
    while (offset < wtf8.size()) {
        auto codePoint = decode_wtf8_code_point(wtf8.data(), wtf8.size(), offset);
        if (codePoint.is_err()) return Err(codePoint.unwrap_err());
        if (codePoint.unwrap() <= 0xFFFFu) {
            append_mutf8_unit(result, static_cast<u16>(codePoint.unwrap()));
            continue;
        }
        append_mutf8_cp(result, codePoint.unwrap());
    }
    return Ok(MUtf8String(std::move(result)));
}

Result<MUtf8String, EncodingError> latin1_to_mutf8(const u8* data, usize size)
{
    // Latin-1（每字节 0-255 当一个 char）→ modified UTF-8：
    //   0x00       → 0xC0 0x80（MUTF-8 对 0x00 的特殊双字节编码）
    //   0x01-0x7F  → 单字节原样
    //   0x80-0xFF  → 双字节 0xC0|(c>>6), 0x80|(c&0x3F)
    // 用于字节流载体（非合法 UTF-8，但每字节是独立的 Latin-1 code point）。
    // 与 utf8_to_mutf8 不同：本函数不把输入当 UTF-8 解码，每字节独立处理，
    // 对任意字节内容都不会因编码原因失败；唯一错误是 data 为空且 size 非零。
    if (data == nullptr && size != 0) {
        return Err(make_error(EncodingErrorCode::kInvalidInput, 0, "null"));
    }
    std::string out;
    out.reserve(size);   // 下界；含高位字节时会更大
    for (usize i = 0; i < size; ++i) {
        u8 c = data[i];
        if (c == 0x00) {
            out.push_back(static_cast<char>(0xC0));
            out.push_back(static_cast<char>(0x80));
        }
        else if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        }
        else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return Ok(MUtf8String(std::move(out)));
}

Result<void, EncodingError> validate_mutf8(MUtf8StringRef m)
{
    return validate_mutf8(m, ConversionValidationMode::kStrict);
}

Result<void, EncodingError> validate_utf8(const std::string& u)
{
    return validate_utf8(u, ConversionValidationMode::kStrict);
}

Result<void, EncodingError> validate_utf8(const std::string& u, ConversionValidationMode mode)
{
    // 校验入口不做有损替换：kLenient 在此与 kStrict 行为一致
    // （lenient 只有搭配 allow_lossy_replacement 才放行畸形输入），mode 仅为 API 对称保留。
    (void)mode;
    auto c = validate_common(u.data(), u.size(), "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (u.empty()) return Ok();
    return validate_utf8_strict_bytes(u.data(), u.size());
}

Result<void, EncodingError> validate_mutf8(MUtf8StringRef m, ConversionValidationMode mode)
{
    // 同 validate_utf8：校验恒按 strict 执行，不做有损替换。
    (void)mode;
    auto c = validate_common(m.data(), m.size(), "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (m.empty()) return Ok();
    return validate_mutf8_strict_bytes(m.data(), m.size());
}

Result<std::string, EncodingError> mutf8_to_utf8_strict(MUtf8StringRef m)
{
    return mutf8_to_utf8_impl(m, ConversionOptions{});
}
Result<std::string, EncodingError> mutf8_to_utf8_lenient(MUtf8StringRef m)
{
    ConversionOptions o;
    o.mode                    = ConversionValidationMode::kLenient;
    o.allow_lossy_replacement = true;
    return mutf8_to_utf8_impl(m, o);
}
Result<MUtf8String, EncodingError> utf8_to_mutf8_strict(const std::string& u)
{
    return utf8_to_mutf8_impl(u.data(), u.size(), ConversionOptions{});
}
Result<MUtf8String, EncodingError> utf8_to_mutf8_lenient(const std::string& u)
{
    ConversionOptions o;
    o.mode                    = ConversionValidationMode::kLenient;
    o.allow_lossy_replacement = true;
    return utf8_to_mutf8_impl(u.data(), u.size(), o);
}

}   // namespace jstr
