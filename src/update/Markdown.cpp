#include "update/Markdown.h"

#include <cctype>

namespace bsl::update {

namespace {

std::string trim(std::string_view s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return std::string(s.substr(a, b - a));
}

bool startsWith(std::string_view s, std::string_view p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

}

std::string stripInlineMarkdown(std::string_view in)
{
    std::string s(in);
    for (size_t a; (a = s.find("<!--")) != std::string::npos;) {
        const size_t b = s.find("-->", a + 4);
        s.erase(a, b == std::string::npos ? std::string::npos : b + 3 - a);
    }
    std::string out;
    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        if (c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '[')) {
            const size_t open = c == '!' ? i + 1 : i;
            const size_t close = s.find(']', open);
            if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
                const size_t end = s.find(')', close + 2);
                if (end != std::string::npos) {
                    out += s.substr(open + 1, close - open - 1);
                    i = end + 1;
                    continue;
                }
            }
        }
        if ((c == '*' || c == '_' || c == '~') && i + 1 < s.size() && s[i + 1] == c) {
            i += 2;
            continue;
        }
        if (c == '`') {
            ++i;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::vector<MdBlock> parseMarkdown(std::string_view markdown)
{
    std::vector<MdBlock> blocks;
    bool inCode = false;
    std::string paragraph;
    auto flush = [&] {
        if (!paragraph.empty()) {
            blocks.push_back({MdKind::Paragraph, stripInlineMarkdown(paragraph), {}, 0});
            paragraph.clear();
        }
    };

    size_t pos = 0;
    while (pos <= markdown.size()) {
        const size_t nl = markdown.find('\n', pos);
        std::string_view raw = markdown.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? markdown.size() + 1 : nl + 1;
        while (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);

        const std::string line = trim(raw);
        if (startsWith(line, "```") || startsWith(line, "~~~")) {
            flush();
            inCode = !inCode;
            continue;
        }
        if (inCode) {
            blocks.push_back({MdKind::Code, std::string(raw), {}, 0});
            continue;
        }
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.size() >= 3 && (line.find_first_not_of('-') == std::string::npos || line.find_first_not_of('*') == std::string::npos
                                 || line.find_first_not_of('_') == std::string::npos)) {
            flush();
            blocks.push_back({MdKind::Rule, {}, {}, 0});
            continue;
        }
        if (line[0] == '#') {
            flush();
            size_t level = 0;
            while (level < line.size() && line[level] == '#')
                ++level;
            const MdKind kind = level == 1 ? MdKind::Heading1 : level == 2 ? MdKind::Heading2 : MdKind::Heading3;
            blocks.push_back({kind, stripInlineMarkdown(trim(line.substr(level))), {}, 0});
            continue;
        }
        if (line[0] == '>') {
            flush();
            blocks.push_back({MdKind::Quote, stripInlineMarkdown(trim(line.substr(1))), {}, 0});
            continue;
        }
        size_t spaces = 0;
        while (spaces < raw.size() && (raw[spaces] == ' ' || raw[spaces] == '\t'))
            spaces += raw[spaces] == '\t' ? 2 : 1;
        const int indent = static_cast<int>(spaces / 2);
        if (line.size() > 1 && (line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
            flush();
            blocks.push_back({MdKind::Bullet, stripInlineMarkdown(trim(line.substr(2))), {}, indent});
            continue;
        }
        size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])))
            ++digits;
        if (digits > 0 && digits < 4 && digits + 1 < line.size() && line[digits] == '.' && line[digits + 1] == ' ') {
            flush();
            blocks.push_back({MdKind::Numbered, stripInlineMarkdown(trim(line.substr(digits + 2))), line.substr(0, digits + 1), indent});
            continue;
        }
        if (!paragraph.empty())
            paragraph.push_back(' ');
        paragraph += line;
    }
    flush();
    return blocks;
}

}
