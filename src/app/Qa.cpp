#include "app/Qa.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace bsl::qa {

namespace {

struct Item
{
    unsigned id = 0;
    float r[4] = {0, 0, 0, 0};
    std::string window;
    std::string label;
    bool visible = true;
    bool isWindow = false;
    bool disabled = false;
};

struct State
{
    bool on = false;
    std::filesystem::path dir;
    std::vector<Item> items;
    std::unordered_map<unsigned, size_t> seen;
    std::set<std::string> conflictKeys;
    std::vector<std::string> conflicts;
    std::set<unsigned> idDebugLogged;
    int cmdSerial = 0;
    std::string pendingLabel;
};
State g_qa;

ImVec2 origin(const ImGuiContext* g)
{
    return g->Viewports.Size > 0 ? g->Viewports[0]->Pos : ImVec2(0.0f, 0.0f);
}

int g_assertCount = 0;
std::vector<std::string> g_asserts;

std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

void writeFileAtomic(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << text;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
}

void addConflict(const std::string& window, const std::string& label, unsigned id, const float* r = nullptr)
{
    char idText[16];
    std::snprintf(idText, sizeof(idText), "0x%08X", id);
    const std::string key = window + "|" + label + "|" + idText;
    if (!g_qa.conflictKeys.insert(key).second)
        return;
    std::string msg = "conflito de ID " + std::string(idText) + " na janela \"" + window + "\" (item \"" + label + "\")";
    if (r) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), " em [%.0f,%.0f,%.0f,%.0f]", r[0], r[1], r[2], r[3]);
        msg += buf;
    }
    g_qa.conflicts.push_back(msg);
    log::warn("[qa] " + msg);
}

}

bool enabled()
{
    return g_qa.on;
}

void init(const std::filesystem::path& dir)
{
    g_qa.on = true;
    g_qa.dir = dir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    ImGuiContext* g = ImGui::GetCurrentContext();
    g->TestEngineHookItems = true;
    g->IO.ConfigDebugHighlightIdConflicts = true;
    log::info("[qa] modo de teste de interface ligado: " + paths::toUtf8(dir));
}

void label(const char* name)
{
    if (g_qa.on)
        g_qa.pendingLabel = name;
}

void beginFrame()
{
    g_qa.pendingLabel.clear();
    g_qa.items.clear();
    g_qa.seen.clear();
}

void note(const std::string& name)
{
    if (!g_qa.on)
        return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    Item it;
    it.label = name;
    it.window = g.CurrentWindow ? g.CurrentWindow->Name : "";
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 o = origin(&g);
    it.r[0] = a.x - o.x;
    it.r[1] = a.y - o.y;
    it.r[2] = b.x - o.x;
    it.r[3] = b.y - o.y;
    it.visible = g.CurrentWindow ? g.CurrentWindow->ClipRect.Overlaps(ImRect(a, b)) : true;
    g_qa.items.push_back(std::move(it));
}

void noteRect(const std::string& name, float x0, float y0, float x1, float y1)
{
    if (!g_qa.on)
        return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    Item it;
    it.label = name;
    it.window = g.CurrentWindow ? g.CurrentWindow->Name : "";
    const ImVec2 o = origin(&g);
    it.r[0] = x0 - o.x;
    it.r[1] = y0 - o.y;
    it.r[2] = x1 - o.x;
    it.r[3] = y1 - o.y;
    g_qa.items.push_back(std::move(it));
}

void endFrame(int frame, const std::string& stateJson, float scale, int fbW, int fbH, int winW, int winH)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();

    if (g.DebugDrawIdConflictsId != 0 && g_qa.idDebugLogged.insert(g.DebugDrawIdConflictsId).second) {
        std::string label;
        for (const Item& it : g_qa.items)
            if (it.id == g.DebugDrawIdConflictsId)
                label = it.label;
        addConflict(g.HoveredWindow ? g.HoveredWindow->Name : (g.CurrentWindow ? g.CurrentWindow->Name : "?"), label,
                    g.DebugDrawIdConflictsId);
    }

    if (frame % 3 != 0)
        return;

    std::set<const ImGuiTabBar*> doneBars;
    for (ImGuiWindow* w : g.Windows) {
        if (!w->DockNode || !w->DockNode->TabBar || !doneBars.insert(w->DockNode->TabBar).second)
            continue;
        const ImGuiTabBar* bar = w->DockNode->TabBar;
        for (const ImGuiTabItem& tab : bar->Tabs) {
            if (!tab.Window || !tab.Window->WasActive)
                continue;
            std::string name = tab.Window->Name;
            const size_t hash = name.find("###");
            if (hash != std::string::npos)
                name = name.substr(0, hash);
            Item it;
            it.window = "dock-tabs";
            it.label = "tab:" + name;
            const ImVec2 o = origin(&g);
            it.r[0] = bar->BarRect.Min.x + tab.Offset - bar->ScrollingAnim - o.x;
            it.r[1] = bar->BarRect.Min.y - o.y;
            it.r[2] = it.r[0] + tab.Width;
            it.r[3] = bar->BarRect.Max.y - o.y;
            it.visible = it.r[2] + o.x > bar->BarRect.Min.x && it.r[0] + o.x < bar->BarRect.Max.x;
            if ((tab.Flags & ImGuiTabItemFlags_NoCloseButton) == 0) {
                const float closeW = std::min(tab.Width * 0.5f, g.FontSize + g.Style.FramePadding.x * 2.0f);
                Item x = it;
                x.label = "tabx:" + name;
                x.r[0] = it.r[2] - closeW;
                it.r[2] -= closeW;
                g_qa.items.push_back(std::move(x));
            }
            g_qa.items.push_back(std::move(it));
        }
    }

    std::ostringstream os;
    os << "{\"frame\":" << frame << ",\"scale\":" << scale << ",\"fb\":[" << fbW << "," << fbH << "],\"win\":[" << winW
       << "," << winH << "],\"asserts\":" << g_assertCount << ",\"conflicts\":" << g_qa.conflicts.size()
       << ",\"state\":" << (stateJson.empty() ? "{}" : stateJson) << ",\"items\":[";
    bool first = true;
    for (const Item& it : g_qa.items) {
        if (!first)
            os << ",";
        first = false;
        os << "{\"w\":\"" << jsonEscape(it.window) << "\",\"l\":\"" << jsonEscape(it.label) << "\",\"r\":[" << it.r[0]
           << "," << it.r[1] << "," << it.r[2] << "," << it.r[3] << "],\"v\":" << (it.visible ? 1 : 0)
           << ",\"win\":" << (it.isWindow ? 1 : 0) << ",\"dis\":" << (it.disabled ? 1 : 0) << "}";
    }
    os << "]}";
    writeFileAtomic(g_qa.dir / L"snapshot.json", os.str());
}

std::vector<std::string> takeCommands()
{
    std::vector<std::string> lines;
    if (!g_qa.on)
        return lines;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(g_qa.dir, ec)) {
        const std::wstring name = e.path().filename().wstring();
        if (name.rfind(L"cmd.", 0) == 0 && e.path().extension() == L".txt")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        {
            std::ifstream in(f, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                    line.pop_back();
                if (!line.empty())
                    lines.push_back(line);
            }
        }
        std::filesystem::remove(f, ec);
    }
    return lines;
}

void finish()
{
    if (!g_qa.on)
        return;
    std::ostringstream os;
    os << "{\"asserts\":" << g_assertCount << ",\"conflicts\":[";
    for (size_t i = 0; i < g_qa.conflicts.size(); ++i)
        os << (i ? "," : "") << "\"" << jsonEscape(g_qa.conflicts[i]) << "\"";
    os << "],\"assertList\":[";
    for (size_t i = 0; i < g_asserts.size(); ++i)
        os << (i ? "," : "") << "\"" << jsonEscape(g_asserts[i]) << "\"";
    os << "]}";
    writeFileAtomic(g_qa.dir / L"summary.json", os.str());
    log::info("[qa] resumo: " + std::to_string(g_assertCount) + " assert(s) do ImGui, " + std::to_string(g_qa.conflicts.size())
              + " conflito(s) de ID");
    g_qa.on = false;
}

}

void bslImGuiAssertFailed(const char* expr, const char* file, int line)
{
    ++bsl::qa::g_assertCount;
    const std::string msg = std::string("assert do ImGui: ") + expr + " (" + file + ":" + std::to_string(line) + ")";
    if (bsl::qa::g_asserts.size() < 40) {
        bsl::qa::g_asserts.push_back(msg);
        bsl::log::warn("[imgui] " + msg);
    }
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* itemData)
{
    using namespace bsl::qa;
    if (!g_qa.on)
        return;
    Item it;
    it.id = id;
    const ImVec2 o = origin(ctx);
    it.r[0] = bb.Min.x - o.x;
    it.r[1] = bb.Min.y - o.y;
    it.r[2] = bb.Max.x - o.x;
    it.r[3] = bb.Max.y - o.y;
    ImGuiWindow* window = ctx->CurrentWindow;
    it.window = window ? window->Name : "";
    it.isWindow = itemData == nullptr && window && id == window->ID;
    it.visible = window ? window->ClipRect.Overlaps(bb) : true;
    if (itemData)
        it.disabled = (itemData->ItemFlags & ImGuiItemFlags_Disabled) != 0;
    if (itemData && !g_qa.pendingLabel.empty()) {
        it.label = g_qa.pendingLabel;
        g_qa.pendingLabel.clear();
    }
    const bool allowDup = itemData && (itemData->ItemFlags & ImGuiItemFlags_AllowDuplicateId) != 0;

    auto found = g_qa.seen.find(id);
    if (id != 0 && itemData && !allowDup && found != g_qa.seen.end() && !g_qa.items[found->second].isWindow) {
        const Item& prev = g_qa.items[found->second];
        const bool sameRect = prev.r[0] == it.r[0] && prev.r[1] == it.r[1] && prev.r[2] == it.r[2] && prev.r[3] == it.r[3];
        if (prev.window == it.window && !sameRect)
            addConflict(it.window, prev.label, id, it.r);
    }
    g_qa.items.push_back(std::move(it));
    if (id != 0)
        g_qa.seen[id] = g_qa.items.size() - 1;
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
    using namespace bsl::qa;
    if (!g_qa.on || id == 0 || !label)
        return;
    const auto found = g_qa.seen.find(id);
    if (found != g_qa.seen.end() && g_qa.items[found->second].label.empty())
        g_qa.items[found->second].label = label;
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
    return nullptr;
}
