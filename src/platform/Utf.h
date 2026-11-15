#pragma once

#include <string>
#include <string_view>

namespace bslplat {

std::wstring utf8ToWide(std::string_view s);
std::string wideToUtf8(std::wstring_view s);

}
