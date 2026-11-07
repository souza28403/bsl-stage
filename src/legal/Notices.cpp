#include "legal/Notices.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace bsl::legal {

namespace {

std::string trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

std::string plain(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '`')
            continue;
        if (in[i] == '*' && i + 1 < in.size() && in[i + 1] == '*') {
            ++i;
            continue;
        }
        out.push_back(in[i]);
    }
    return trim(out);
}

std::vector<std::string> licensePaths(const std::string& value)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t a = value.find('`', pos);
        if (a == std::string::npos)
            break;
        const size_t b = value.find('`', a + 1);
        if (b == std::string::npos)
            break;
        const std::string token = value.substr(a + 1, b - a - 1);
        if (startsWith(token, "licenses/"))
            out.push_back(token);
        pos = b + 1;
    }
    return out;
}

size_t utf8Length(const std::string& s)
{
    size_t n = 0;
    for (const unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n;
    return n;
}

size_t utf8Prefix(const std::string& word, size_t columns)
{
    size_t chars = 0;
    for (size_t i = 0; i < word.size(); ++i) {
        if ((static_cast<unsigned char>(word[i]) & 0xC0) != 0x80) {
            if (chars == columns)
                return i;
            ++chars;
        }
    }
    return word.size();
}

}

std::vector<ThirdParty> parseThirdParty(const std::string& markdown)
{
    std::vector<ThirdParty> list;
    ThirdParty* current = nullptr;
    size_t pos = 0;
    while (pos <= markdown.size()) {
        const size_t nl = markdown.find('\n', pos);
        std::string line = markdown.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? markdown.size() + 1 : nl + 1;
        line = trim(line);

        if (startsWith(line, "### ")) {
            list.emplace_back();
            current = &list.back();
            current->name = plain(line.substr(4));
            continue;
        }
        if (startsWith(line, "## ") || startsWith(line, "# ")) {
            current = nullptr;
            continue;
        }
        if (!current || !startsWith(line, "- **"))
            continue;
        const size_t keyEnd = line.find(":**", 4);
        if (keyEnd == std::string::npos)
            continue;
        const std::string key = line.substr(4, keyEnd - 4);
        const std::string raw = trim(line.substr(keyEnd + 3));
        if (key == "Versão")
            current->version = plain(raw);
        else if (key == "Licença")
            current->license = plain(raw);
        else if (key == "Copyright")
            current->copyright = plain(raw);
        else if (key == "URL")
            current->url = plain(raw);
        else if (key == "Uso")
            current->usage = plain(raw);
        else if (key == "Situação")
            current->status = plain(raw);
        else if (key == "Texto da licença")
            for (std::string& p : licensePaths(raw))
                current->licenseFiles.push_back(std::move(p));
    }
    return list;
}

bool safeLicensePath(const std::string& relative)
{
    if (!startsWith(relative, "licenses/") || relative.size() <= 9)
        return false;
    if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos
        || relative.find(':') != std::string::npos)
        return false;
    return true;
}

std::string readTextFile(const std::filesystem::path& file, size_t maxBytes)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    if (text.size() > maxBytes)
        text.resize(maxBytes);
    return text;
}

std::filesystem::path findDocsRoot(const std::filesystem::path& startDir)
{
    std::filesystem::path dir = startDir;
    std::error_code ec;
    for (int i = 0; i < 6 && !dir.empty(); ++i) {
        if (std::filesystem::is_regular_file(dir / L"THIRD-PARTY-NOTICES.md", ec))
            return dir;
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

std::filesystem::path docsRoot()
{
    static const std::filesystem::path cached = [] {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return findDocsRoot(std::filesystem::path(std::wstring(buf, n)).parent_path());
    }();
    return cached;
}

std::vector<std::string> wrapText(const std::string& text, int columns)
{
    const size_t width = static_cast<size_t>(std::max(columns, 8));
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        for (size_t t; (t = line.find('\t')) != std::string::npos;)
            line.replace(t, 1, "    ");
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        if (utf8Length(line) <= width) {
            out.push_back(line);
            continue;
        }

        const size_t indentLen = std::min(line.find_first_not_of(' ') == std::string::npos ? size_t{0} : line.find_first_not_of(' '),
                                          width / 2);
        const std::string indent(indentLen, ' ');
        std::string row;
        size_t i = indentLen;
        row = indent;
        bool rowHasWord = false;
        while (i < line.size()) {
            while (i < line.size() && line[i] == ' ')
                ++i;
            if (i >= line.size())
                break;
            size_t end = line.find(' ', i);
            if (end == std::string::npos)
                end = line.size();
            std::string word = line.substr(i, end - i);
            i = end;
            while (true) {
                const size_t add = utf8Length(word) + (rowHasWord ? 1u : 0u);
                if (utf8Length(row) + add <= width) {
                    if (rowHasWord)
                        row.push_back(' ');
                    row += word;
                    rowHasWord = true;
                    break;
                }
                if (rowHasWord) {
                    out.push_back(row);
                    row = indent;
                    rowHasWord = false;
                    continue;
                }
                const size_t cut = utf8Prefix(word, width - indentLen);
                out.push_back(row + word.substr(0, cut));
                word.erase(0, cut);
                row = indent;
                if (word.empty())
                    break;
            }
        }
        if (rowHasWord)
            out.push_back(row);
    }
    return out;
}

}
