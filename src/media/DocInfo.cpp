#include "media/DocInfo.h"

#include <map>
#include <mutex>
#include <string>

namespace bsl::media {

namespace {

std::mutex g_mutex;
std::map<std::wstring, int>& table()
{
    static std::map<std::wstring, int> t;
    return t;
}

std::wstring keyOf(const std::filesystem::path& p)
{
    std::error_code ec;
    std::wstring s = std::filesystem::absolute(p, ec).lexically_normal().wstring();
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(::towlower(c));
    return s;
}

} // namespace

void setDocPageCount(const std::filesystem::path& file, int pages)
{
    if (pages <= 0)
        return;
    std::lock_guard lk(g_mutex);
    table()[keyOf(file)] = pages;
}

int docPageCount(const std::filesystem::path& file)
{
    std::lock_guard lk(g_mutex);
    const auto it = table().find(keyOf(file));
    return it == table().end() ? 0 : it->second;
}

} // namespace bsl::media
