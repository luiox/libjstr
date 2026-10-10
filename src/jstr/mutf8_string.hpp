#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "libca/core/datatype.hpp"

/// @file mutf8_string.hpp
/// @brief Owning and non-owning containers for Modified UTF-8 (MUTF-8) bytes.
///
/// MUTF-8 is the JVM class-file string encoding (NUL as C0 80, supplementary
/// code points as surrogate pairs). These types only *hold* bytes — they do not
/// validate or transcode; use conversion.hpp for that. No method here throws
/// except where the underlying std::string allocation can (noted per type).

namespace jstr {

// ============================================================================
// MUtf8StringRef — non-owning MUTF-8 byte sequence view
// ============================================================================

class MUtf8String;

/// @brief Non-owning view over a MUTF-8 byte sequence (pointer + length).
///
/// Borrows external bytes (class-file buffer, mmap region, literal) with zero
/// copy. All members are noexcept.
///
/// @warning The view does not own its bytes and must not outlive them. The bytes
///          are assumed to already be MUTF-8; no validation is performed here.
class MUtf8StringRef
{
public:
    /// Constructs an empty view (data()==nullptr, size()==0).
    MUtf8StringRef() noexcept = default;

    /// Views @p size bytes at @p data. The caller owns the bytes' lifetime.
    MUtf8StringRef(const char* data, ca::usize size) noexcept;

    /// Views the bytes of @p view. @p view's underlying storage must outlive this.
    explicit MUtf8StringRef(std::string_view view) noexcept;

    /// Builds a view over a NUL-terminated C string; returns an empty view if
    /// @p c_str is nullptr.
    static MUtf8StringRef from_c_str(const char* c_str) noexcept;

    const char* data() const noexcept { return data_; }
    ca::usize   size() const noexcept { return size_; }
    bool        empty() const noexcept { return size_ == 0; }

    const char* begin() const noexcept { return data_; }
    const char* end() const noexcept { return data_ + size_; }

    /// Reinterprets the same bytes as a std::string_view (no copy).
    std::string_view as_std_view() const noexcept;

private:
    const char* data_ = nullptr;
    ca::usize   size_ = 0;
};

// ============================================================================
// MUtf8String — owned MUTF-8 byte container
// ============================================================================

/// @brief Owning container of MUTF-8 bytes (backed by std::string).
///
/// Holds an independent copy of the bytes. Stored bytes are assumed to be
/// MUTF-8; the class neither validates nor transcodes (see conversion.hpp).
///
/// @note Constructors allocate and may throw std::bad_alloc / std::length_error.
///       All accessors are noexcept.
class MUtf8String
{
public:
    MUtf8String() = default;
    /// Takes ownership of @p bytes (moved in).
    explicit MUtf8String(std::string bytes);
    /// Copies raw bytes from a byte vector.
    explicit MUtf8String(const std::vector<ca::u8>& bytes);

    const char* data() const noexcept;
    ca::usize   size() const noexcept;
    bool        empty() const noexcept;

    /// Returns a non-owning view; valid only while this object is alive.
    MUtf8StringRef view() const noexcept;

    const std::string& bytes() const noexcept { return bytes_; }

private:
    std::string bytes_;
};

};   // namespace jstr
