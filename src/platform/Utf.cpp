#include "platform/Utf.h"

namespace bslplat {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void appendWide(std::wstring& out, char32_t cp)
{
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
            return;
        }
    }
    out.push_back(static_cast<wchar_t>(cp));
}

}

std::wstring utf8ToWide(std::string_view s)
{
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        char32_t cp = kReplacement;
        std::size_t len = 1;
        if (b < 0x80) {
            cp = b;
        } else if (b >= 0xC2 && b < 0xE0) {
            len = 2;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b < 0xF0) {
            len = 3;
            cp = b & 0x0F;
        } else if (b >= 0xF0 && b < 0xF5) {
            len = 4;
            cp = b & 0x07;
        }
        if (len > 1) {
            bool ok = i + len <= s.size();
            for (std::size_t k = 1; ok && k < len; ++k) {
                const auto c = static_cast<unsigned char>(s[i + k]);
                if ((c & 0xC0) != 0x80)
                    ok = false;
                else
                    cp = (cp << 6) | (c & 0x3F);
            }
            if (!ok || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || (len == 3 && cp < 0x800)
                || (len == 4 && cp < 0x10000)) {
                cp = kReplacement;
                len = 1;
            }
        }
        appendWide(out, cp);
        i += len;
    }
    return out;
}

std::string wideToUtf8(std::wstring_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char32_t cp = static_cast<char32_t>(s[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && static_cast<char32_t>(s[i + 1]) >= 0xDC00
                && static_cast<char32_t>(s[i + 1]) < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(s[i + 1]) - 0xDC00);
                ++i;
            }
        }
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
            cp = kReplacement;
        appendUtf8(out, cp);
    }
    return out;
}

}
