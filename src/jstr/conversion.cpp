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

bool append_mutf8_cp(std::string& out, u32 cp)
{
    if (cp > 0x10FFFFu) return false;
    if (cp <= 0xFFFFu) {
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
        append_mutf8_unit(out, (u16)cp);
        return true;
    }
    u32 v = cp - 0x10000u;
    append_mutf8_unit(out, (u16)(0xD800u + (v >> 10)));
    append_mutf8_unit(out, (u16)(0xDC00u + (v & 0x3FFu)));
    return true;
}

// ===== UTF-8 decode with error reporting =====

Result<u32, EncodingError> decode_utf8_strict(const char* d, usize sz, usize& off)
{
    usize start = off;
    u8    b0    = (u8)d[off];
    usize clen  = ca::str::utf8_code_point_bytes(b0);
    if (clen == 0 || off + clen > sz)
        return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad utf8"));
    if (clen == 1) {
        ++off;
        return Ok((u32)b0);
    }
    if (clen == 2) {
        u8 b1 = (u8)d[off + 1];
        if (!is_continuation_byte(b1))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start + 1, "bad cont"));
        off += 2;
        return Ok((u32)(((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu)));
    }
    if (clen == 3) {
        u8 b1 = (u8)d[off + 1], b2 = (u8)d[off + 2];
        if (!is_continuation_byte(b1) || !is_continuation_byte(b2))
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad 3b"));
        if (b0 == 0xE0u && b1 < 0xA0u)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "overlong"));
        if (b0 == 0xEDu && b1 > 0x9Fu)
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "surrogate"));
        off += 3;
        return Ok((u32)(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)));
    }
    u8 b1 = (u8)d[off + 1], b2 = (u8)d[off + 2], b3 = (u8)d[off + 3];
    if (!is_continuation_byte(b1) || !is_continuation_byte(b2) || !is_continuation_byte(b3))
        return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad 4b"));
    if ((b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu))
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
    if (off + 2 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
    u8 b1 = (u8)d[off + 1], b2 = (u8)d[off + 2];
    if (!is_continuation_byte(b1) || !is_continuation_byte(b2))
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "bad 3b"));
    if (b0 == 0xE0u && (b1 < 0xA0u || b1 > 0xBFu))
        return Err(make_error(EncodingErrorCode::kInvalidMutf8Sequence, start, "overlong"));
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
        if (off + 2 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = static_cast<u8>(d[off + 1]);
        u8 b2 = static_cast<u8>(d[off + 2]);
        if (!is_continuation_byte(b1) || !is_continuation_byte(b2) ||
            (b0 == 0xE0u && b1 < 0xA0u)) {
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad 3b"));
        }
        off += 3;
        return Ok(static_cast<u32>(((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu)));
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (off + 3 >= sz) return Err(make_error(EncodingErrorCode::kUnexpectedEnd, start, "trunc"));
        u8 b1 = static_cast<u8>(d[off + 1]);
        u8 b2 = static_cast<u8>(d[off + 2]);
        u8 b3 = static_cast<u8>(d[off + 3]);
        if (!is_continuation_byte(b1) || !is_continuation_byte(b2) || !is_continuation_byte(b3) ||
            (b0 == 0xF0u && b1 < 0x90u) || (b0 == 0xF4u && b1 > 0x8Fu)) {
            return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad 4b"));
        }
        off += 4;
        return Ok(static_cast<u32>(((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) |
                                   ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu)));
    }
    return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, start, "bad lead"));
}

// ===== validation =====

Result<void, EncodingError> validate_utf8_strict_bytes(const char* d, usize sz)
{
    if (!ca::str::utf8_is_valid((const u8*)d, sz))
        return Err(make_error(EncodingErrorCode::kInvalidUtf8Sequence, 0, "invalid utf8"));
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
                if (wl) {
                    append_replacement(u);
                    wl = false;
                }
                append_replacement(u);
                off = st + 1;
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
                append_mutf8_cp(m, kReplacementCodePoint);
                off = st + 1;
                continue;
            }
            return Err(r.unwrap_err());
        }
        u32 cp = r.unwrap();
        if (!append_mutf8_cp(m, cp)) {
            if (opts.mode == ConversionValidationMode::kLenient && opts.allow_lossy_replacement) {
                append_mutf8_cp(m, kReplacementCodePoint);
                continue;
            }
            return Err(make_error(EncodingErrorCode::kUnsupportedCodePoint, st, "unsup"));
        }
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
        if (!append_mutf8_cp(result, codePoint.unwrap())) {
            return Err(make_error(EncodingErrorCode::kUnsupportedCodePoint, offset, "unsup"));
        }
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
    // 与 utf8_to_mutf8 不同：本函数不把输入当 UTF-8 解码，每字节独立处理，永不失败。
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
    auto c = validate_common(u.data(), u.size(), "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (u.empty()) return Ok();
    if (mode == ConversionValidationMode::kStrict)
        return validate_utf8_strict_bytes(u.data(), u.size());
    return Ok();
}

Result<void, EncodingError> validate_mutf8(MUtf8StringRef m, ConversionValidationMode mode)
{
    auto c = validate_common(m.data(), m.size(), "null");
    if (c.is_err()) return Err(c.unwrap_err());
    if (m.empty()) return Ok();
    if (mode == ConversionValidationMode::kStrict)
        return validate_mutf8_strict_bytes(m.data(), m.size());
    return Ok();
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
