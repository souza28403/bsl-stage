#include "media/CaptureTargets.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace bsl::media {

namespace {

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::set<std::string> tokens(const std::string& s)
{
    std::set<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c >= 0x80) {
            cur += static_cast<char>(std::tolower(c));
        } else if (!cur.empty()) {
            out.insert(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        out.insert(cur);
    return out;
}

}

double titleSimilarity(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return a == b ? 1.0 : 0.0;
    const std::string la = lowerAscii(a), lb = lowerAscii(b);
    if (la == lb)
        return 1.0;
    size_t common = 0;
    while (common < la.size() && common < lb.size() && la[common] == lb[common])
        ++common;
    const double prefix = static_cast<double>(common) / static_cast<double>(std::max(la.size(), lb.size()));

    const std::set<std::string> ta = tokens(la), tb = tokens(lb);
    size_t inter = 0;
    for (const std::string& t : ta)
        inter += tb.count(t);
    const size_t uni = ta.size() + tb.size() - inter;
    const double jaccard = uni ? static_cast<double>(inter) / static_cast<double>(uni) : 0.0;
    return std::max(prefix, jaccard);
}

std::uintptr_t findWindow(const std::string& exe, const std::string& title)
{
    const std::string wantExe = lowerAscii(exe);
    std::vector<WindowTarget> candidates;
    for (WindowTarget& w : listWindows())
        if (wantExe.empty() || w.exe == wantExe)
            candidates.push_back(std::move(w));
    if (candidates.empty())
        return 0;

    const WindowTarget* best = nullptr;
    double bestScore = -1.0;
    for (const WindowTarget& w : candidates) {
        const double s = titleSimilarity(w.title, title);
        if (s > bestScore) {
            bestScore = s;
            best = &w;
        }
    }
    if (best && (bestScore >= 0.5 || candidates.size() == 1))
        return best->hwnd;
    return 0;
}

Json monitorTargetJson(int index)
{
    return Json{{"kind", "monitor"}, {"index", index}};
}

Json windowTargetJson(const WindowTarget& w)
{
    return Json{{"kind", "window"}, {"exe", w.exe}, {"title", w.title}};
}

std::string targetLabel(const Json& target)
{
    if (!target.is_object())
        return "Monitor 1";
    const std::string kind = target.value("kind", std::string("monitor"));
    if (kind == "window") {
        std::string title = target.value("title", std::string());
        const std::string exe = target.value("exe", std::string());
        if (title.size() > 48) {
            size_t cut = 45;
            while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80)
                --cut;
            title = title.substr(0, cut) + "...";
        }
        return "Janela: " + (title.empty() ? exe : title) + (exe.empty() ? "" : " (" + exe + ")");
    }
    const int index = target.contains("index") && target["index"].is_number_integer() ? target["index"].get<int>() : 1;
    std::string label = "Monitor " + std::to_string(index);
    for (const MonitorTarget& m : listMonitors())
        if (m.index == index)
            label += " (" + std::to_string(m.w) + "x" + std::to_string(m.h) + ")";
    return label;
}

}
