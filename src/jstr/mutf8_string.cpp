#include "mutf8_string.hpp"
#include <cstring>

using namespace ca;

namespace jstr {

MUtf8StringRef::MUtf8StringRef(const char* data, usize size) noexcept
    : data_(data)
    , size_(size)
{}

MUtf8StringRef::MUtf8StringRef(std::string_view view) noexcept
    : MUtf8StringRef(view.data(), view.size())
{}

MUtf8StringRef MUtf8StringRef::from_c_str(const char* c_str) noexcept
{
    if (c_str == nullptr) return MUtf8StringRef();
    return MUtf8StringRef(c_str, std::strlen(c_str));
}

std::string_view MUtf8StringRef::as_std_view() const noexcept
{
    return std::string_view(data_, size_);
}

MUtf8String::MUtf8String(std::string bytes)
    : bytes_(std::move(bytes))
{}

MUtf8String::MUtf8String(const std::vector<u8>& bytes)
{
    if (!bytes.empty()) bytes_.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

const char* MUtf8String::data() const noexcept
{
    return bytes_.data();
}
usize MUtf8String::size() const noexcept
{
    return bytes_.size();
}
bool MUtf8String::empty() const noexcept
{
    return bytes_.empty();
}

MUtf8StringRef MUtf8String::view() const noexcept
{
    return MUtf8StringRef(data(), size());
}

}   // namespace jstr
