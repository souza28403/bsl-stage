#include "update/IssueReport.h"

#include <algorithm>
#include <cctype>

namespace bsl::update {

namespace {

std::string trimmed(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string normalizeNewlines(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            if (i + 1 < s.size() && s[i + 1] == '\n')
                continue;
            out.push_back('\n');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool isUtf8Continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

size_t encodedSize(std::string_view s)
{
    size_t n = 0;
    for (unsigned char c : s)
        n += (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') ? 1 : 3;
    return n;
}

std::string replaceAllCI(std::string text, const std::string& from, const std::string& to)
{
    if (from.size() < 2)
        return text;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const std::string lowText = lower(text), lowFrom = lower(from);
    std::string out;
    size_t pos = 0;
    while (true) {
        const size_t hit = lowText.find(lowFrom, pos);
        if (hit == std::string::npos) {
            out += text.substr(pos);
            break;
        }
        out += text.substr(pos, hit - pos) + to;
        pos = hit + from.size();
    }
    return out;
}

}

const char* labelFor(ReportKind kind)
{
    return kind == ReportKind::Bug ? "bug" : "enhancement";
}

std::string buildReportBody(const ReportInput& in)
{
    const bool bug = in.kind == ReportKind::Bug;
    std::string body;
    auto section = [&](const char* heading, const std::string& text) {
        if (!body.empty())
            body += "\n\n";
        body += std::string("### ") + heading + "\n\n" + text;
    };
    const std::string desc = trimmed(normalizeNewlines(in.description));
    section(bug ? "Descrição do problema" : "Descrição da ideia", desc.empty() ? "_(não informado)_" : desc);
    const std::string steps = trimmed(normalizeNewlines(in.steps));
    if (!steps.empty())
        section(bug ? "Passos para reproduzir" : "Exemplo de uso", steps);
    if (in.includeSystem) {
        const SystemInfo& s = in.system;
        std::string text = "- BSL Stage: " + s.appVersion + "\n- Windows: " + s.windows + "\n- GPU: " + s.gpu
                           + "\n- OpenGL / driver: " + s.openGl + "\n- Perfil de desempenho: " + s.profile + "\n- Memória (RAM): " + s.ram;
        section("Informações do sistema", text);
    }
    if (in.includeLog) {
        const std::string log = trimmed(normalizeNewlines(in.logTail));
        section("Últimas linhas do log", "```\n" + (log.empty() ? std::string("(log vazio)") : log) + "\n```");
    }
    return body;
}

std::string urlEncode(std::string_view text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 2);
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

IssueUrl buildIssueUrl(const std::string& repo, ReportKind kind, const std::string& titleIn, const std::string& bodyIn,
                       size_t maxUrl)
{
    IssueUrl r;
    std::string title = trimmed(normalizeNewlines(titleIn));
    std::replace(title.begin(), title.end(), '\n', ' ');
    if (title.size() > 200) {
        size_t cut = 200;
        while (cut > 0 && isUtf8Continuation(title[cut]))
            --cut;
        title.resize(cut);
    }
    const std::string body = normalizeNewlines(bodyIn);
    const std::string head = "https://github.com/" + repo + "/issues/new?title=" + urlEncode(title) + "&body=";
    const std::string tail = std::string("&labels=") + labelFor(kind);
    const size_t fixed = head.size() + tail.size();

    if (fixed + encodedSize(body) <= maxUrl) {
        r.url = head + urlEncode(body) + tail;
        r.bodyInUrl = body;
        return r;
    }

    const std::string notice = "\n\n_[texto cortado: o resto está na área de transferência; cole aqui com Ctrl+V]_";
    const size_t budget = maxUrl > fixed + encodedSize(notice) ? maxUrl - fixed - encodedSize(notice) : 0;
    size_t cut = 0, used = 0;
    while (cut < body.size()) {
        const unsigned char c = static_cast<unsigned char>(body[cut]);
        const size_t cost = (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') ? 1 : 3;
        if (used + cost > budget)
            break;
        used += cost;
        ++cut;
    }
    while (cut > 0 && cut < body.size() && isUtf8Continuation(body[cut]))
        --cut;
    const size_t nlPos = body.rfind('\n', cut == 0 ? 0 : cut - 1);
    if (nlPos != std::string::npos && nlPos > cut / 2 && cut < body.size())
        cut = nlPos + 1;

    r.truncated = true;
    r.remainder = body.substr(cut);
    r.bodyInUrl = body.substr(0, cut) + notice;
    r.url = head + urlEncode(r.bodyInUrl) + tail;
    return r;
}

std::string tailLines(const std::string& textIn, int maxLines, size_t maxBytes)
{
    std::string text = normalizeNewlines(textIn);
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    size_t start = text.size();
    int lines = 0;
    while (start > 0) {
        if (text[start - 1] == '\n') {
            if (++lines >= maxLines)
                break;
        }
        --start;
    }
    std::string out = text.substr(start);
    if (out.size() > maxBytes) {
        size_t cut = out.size() - maxBytes;
        while (cut < out.size() && isUtf8Continuation(out[cut]))
            ++cut;
        const size_t nl = out.find('\n', cut);
        out = nl == std::string::npos ? out.substr(cut) : out.substr(nl + 1);
    }
    return out;
}

std::string redactPersonalData(const std::string& text, const std::string& userProfileDir, const std::string& userName,
                               const std::string& computerName)
{
    std::string out = text;
    out = replaceAllCI(out, userProfileDir, "%USERPROFILE%");
    std::string slashes = userProfileDir;
    std::replace(slashes.begin(), slashes.end(), '\\', '/');
    out = replaceAllCI(out, slashes, "%USERPROFILE%");
    out = replaceAllCI(out, userName, "<usuario>");
    out = replaceAllCI(out, computerName, "<pc>");
    return out;
}

}
