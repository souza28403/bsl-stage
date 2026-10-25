#include "media/CaptureTargets.h"

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cctype>
#include <set>

namespace bsl::media {

namespace {

std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string exeNameOf(DWORD pid)
{
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD len = static_cast<DWORD>(std::size(buf));
    std::string name;
    if (QueryFullProcessImageNameW(h, 0, buf, &len)) {
        std::wstring full(buf, len);
        const size_t slash = full.find_last_of(L"\\/");
        name = toUtf8(slash == std::wstring::npos ? full : full.substr(slash + 1));
    }
    CloseHandle(h);
    return lowerAscii(name);
}

std::string ownExeName()
{
    static const std::string name = exeNameOf(GetCurrentProcessId());
    return name;
}

struct EnumData
{
    std::vector<WindowTarget> out;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lp)
{
    auto* data = reinterpret_cast<EnumData*>(lp);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW))
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr && !(ex & WS_EX_APPWINDOW))
        return TRUE;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return TRUE;

    wchar_t title[512] = {};
    const int len = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (len <= 0)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return TRUE; // janela principal e de saida do BSL Stage
    const std::string exe = exeNameOf(pid);
    if (!exe.empty() && exe == ownExeName())
        return TRUE; // outra instancia do BSL Stage

    RECT r{};
    GetWindowRect(hwnd, &r);
    if (r.right - r.left < 32 || r.bottom - r.top < 32)
        return TRUE;

    WindowTarget w;
    w.hwnd = reinterpret_cast<std::uintptr_t>(hwnd);
    w.title = toUtf8(std::wstring(title, static_cast<size_t>(len)));
    w.exe = exe;
    w.pid = pid;
    w.w = r.right - r.left;
    w.h = r.bottom - r.top;
    data->out.push_back(std::move(w));
    return TRUE;
}

struct MonData
{
    std::vector<MonitorTarget> out;
};

BOOL CALLBACK enumMonitorsProc(HMONITOR mon, HDC, LPRECT, LPARAM lp)
{
    auto* data = reinterpret_cast<MonData*>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;
    MonitorTarget m;
    m.handle = reinterpret_cast<std::uintptr_t>(mon);
    m.x = mi.rcMonitor.left;
    m.y = mi.rcMonitor.top;
    m.w = mi.rcMonitor.right - mi.rcMonitor.left;
    m.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    data->out.push_back(m);
    return TRUE;
}

// Cria um DIB 32 bpp top-down e devolve o ponteiro dos pixels
HBITMAP createDib(HDC dc, int w, int h, void** bits)
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
}

// Reduz 'srcDc' (sw x sh) para caber em w x h com fundo escuro; devolve RGBA
bool shrinkToThumb(HDC srcDc, int srcX, int srcY, int sw, int sh, int w, int h, ThumbRgba& out)
{
    if (sw <= 0 || sh <= 0)
        return false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bmp = createDib(screen, w, h, &bits);
    bool ok = false;
    if (bmp && bits) {
        const HGDIOBJ old = SelectObject(mem, bmp);
        RECT all{0, 0, w, h};
        HBRUSH bg = CreateSolidBrush(RGB(14, 14, 14));
        FillRect(mem, &all, bg);
        DeleteObject(bg);

        const double s = std::min(static_cast<double>(w) / sw, static_cast<double>(h) / sh);
        const int dw = std::max(1, static_cast<int>(sw * s));
        const int dh = std::max(1, static_cast<int>(sh * s));
        SetStretchBltMode(mem, HALFTONE);
        SetBrushOrgEx(mem, 0, 0, nullptr);
        ok = StretchBlt(mem, (w - dw) / 2, (h - dh) / 2, dw, dh, srcDc, srcX, srcY, sw, sh, SRCCOPY | CAPTUREBLT) != 0;
        GdiFlush();

        if (ok) {
            out.w = w;
            out.h = h;
            out.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
            const auto* px = static_cast<const unsigned char*>(bits);
            for (size_t i = 0; i < out.rgba.size(); i += 4) {
                out.rgba[i] = px[i + 2];
                out.rgba[i + 1] = px[i + 1];
                out.rgba[i + 2] = px[i];
                out.rgba[i + 3] = 255;
            }
        }
        SelectObject(mem, old);
    }
    if (bmp)
        DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
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

} // namespace

std::vector<MonitorTarget> listMonitors()
{
    MonData data;
    EnumDisplayMonitors(nullptr, nullptr, enumMonitorsProc, reinterpret_cast<LPARAM>(&data));
    std::sort(data.out.begin(), data.out.end(), [](const MonitorTarget& a, const MonitorTarget& b) {
        if (a.primary != b.primary)
            return a.primary;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    for (size_t i = 0; i < data.out.size(); ++i)
        data.out[i].index = static_cast<int>(i) + 1;
    return data.out;
}

std::vector<WindowTarget> listWindows()
{
    EnumData data;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.out; // EnumWindows ja entrega na ordem Z (a de cima primeiro)
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

bool grabMonitorThumb(const MonitorTarget& m, int w, int h, ThumbRgba& out)
{
    HDC screen = GetDC(nullptr);
    const bool ok = shrinkToThumb(screen, m.x, m.y, m.w, m.h, w, h, out);
    ReleaseDC(nullptr, screen);
    return ok;
}

bool grabWindowThumb(const WindowTarget& win, int w, int h, ThumbRgba& out)
{
    const HWND hwnd = reinterpret_cast<HWND>(win.hwnd);
    if (!IsWindow(hwnd) || IsIconic(hwnd))
        return false;
    RECT r{};
    GetWindowRect(hwnd, &r);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    if (ww <= 0 || wh <= 0 || ww > 8192 || wh > 8192)
        return false;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bmp = createDib(screen, ww, wh, &bits);
    bool ok = false;
    if (bmp && bits) {
        const HGDIOBJ old = SelectObject(mem, bmp);
        // PW_RENDERFULLCONTENT (2): inclui janelas aceleradas por GPU
        if (PrintWindow(hwnd, mem, 2))
            ok = shrinkToThumb(mem, 0, 0, ww, wh, w, h, out);
        SelectObject(mem, old);
    }
    if (bmp)
        DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

} // namespace bsl::media
