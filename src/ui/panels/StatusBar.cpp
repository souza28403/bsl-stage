#include "ui/panels/StatusBar.h"

#include "app/Log.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <glad/gl.h>

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace bsl::ui {

namespace {

// Extensões de memória de vídeo (não estão no glad 3.3 core)
constexpr GLenum kNvxTotalAvailableKb = 0x9048; // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
constexpr GLenum kNvxCurrentAvailableKb = 0x9049; // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
constexpr GLenum kAtiTextureFreeMemory = 0x87FC; // GL_TEXTURE_FREE_MEMORY_ATI

enum class VramSource { Unknown, Nvidia, Ati, None };

struct SystemStats
{
    double cpuPercent = 0.0;
    double ramMb = 0.0;
    VramSource vramSource = VramSource::Unknown;
    double vramUsedMb = 0.0;
    double vramTotalMb = 0.0; // 0 = só sabemos o livre (ATI)
    double vramFreeMb = 0.0;

    std::chrono::steady_clock::time_point lastSample{};
    ULONGLONG lastProcessTime = 0; // kernel + user, em unidades de 100 ns
    bool hasLast = false;
};

SystemStats g_stats;

ULONGLONG fileTimeToU64(const FILETIME& ft)
{
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

bool hasExtension(const char* name)
{
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const auto* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext && std::strcmp(ext, name) == 0)
            return true;
    }
    return false;
}

void sampleVram(SystemStats& s)
{
    if (s.vramSource == VramSource::Unknown) {
        if (hasExtension("GL_NVX_gpu_memory_info"))
            s.vramSource = VramSource::Nvidia;
        else if (hasExtension("GL_ATI_meminfo"))
            s.vramSource = VramSource::Ati;
        else
            s.vramSource = VramSource::None;
    }
    while (glGetError() != GL_NO_ERROR) {
    }
    if (s.vramSource == VramSource::Nvidia) {
        GLint totalKb = 0, freeKb = 0;
        glGetIntegerv(kNvxTotalAvailableKb, &totalKb);
        glGetIntegerv(kNvxCurrentAvailableKb, &freeKb);
        if (glGetError() == GL_NO_ERROR && totalKb > 0) {
            s.vramTotalMb = totalKb / 1024.0;
            s.vramFreeMb = freeKb / 1024.0;
            s.vramUsedMb = s.vramTotalMb - s.vramFreeMb;
        } else {
            s.vramSource = VramSource::None;
        }
    } else if (s.vramSource == VramSource::Ati) {
        GLint info[4] = {0, 0, 0, 0}; // [0] = memória livre total do pool, em KB
        glGetIntegerv(kAtiTextureFreeMemory, info);
        if (glGetError() == GL_NO_ERROR) {
            s.vramFreeMb = info[0] / 1024.0;
            s.vramTotalMb = 0.0;
        } else {
            s.vramSource = VramSource::None;
        }
    }
}

// Atualiza CPU / RAM / VRAM no máximo uma vez por segundo
void sampleSystem(SystemStats& s)
{
    const auto now = std::chrono::steady_clock::now();
    if (s.hasLast && now - s.lastSample < std::chrono::seconds(1))
        return;

    FILETIME creationTime{}, exitTime{}, kernelTime{}, userTime{};
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        const ULONGLONG total = fileTimeToU64(kernelTime) + fileTimeToU64(userTime);
        if (s.hasLast) {
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            const double wall100ns = std::chrono::duration<double>(now - s.lastSample).count() * 1.0e7;
            const double cores = si.dwNumberOfProcessors > 0 ? static_cast<double>(si.dwNumberOfProcessors) : 1.0;
            if (wall100ns > 0.0)
                s.cpuPercent = static_cast<double>(total - s.lastProcessTime) / (wall100ns * cores) * 100.0;
        }
        s.lastProcessTime = total;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        s.ramMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);

    sampleVram(s);

    s.lastSample = now;
    s.hasLast = true;
}

// "rótulo" cinza + valor em mono
void stat(const char* label, const std::string& value, const ImVec4& color)
{
    ImGui::TextColored(theme::kTextDim, "%s", label);
    ImGui::SameLine(0.0f, dp(6.0f));
    ImFont* mono = fonts().mono;
    const ImVec2 size = mono->CalcTextSizeA(fontPx(mono), FLT_MAX, 0.0f, value.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float dy = ImGui::GetStyle().FramePadding.y + (ImGui::GetFontSize() - fontPx(mono)) * 0.5f + dp(1.0f); // alinha a linha de base
    ImGui::GetWindowDrawList()->AddText(mono, fontPx(mono), ImVec2(pos.x, pos.y + dy),
                                        ImGui::GetColorU32(color), value.c_str());
    ImGui::Dummy(size);
}

std::string format(const char* fmt, double a, double b = 0.0)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    return buf;
}

std::string vramText(const SystemStats& s)
{
    switch (s.vramSource) {
    case VramSource::Nvidia:
        return format("%.1f / %.1f GB", s.vramUsedMb / 1024.0, s.vramTotalMb / 1024.0);
    case VramSource::Ati:
        return format("%.1f GB livres", s.vramFreeMb / 1024.0);
    default:
        return "—";
    }
}

} // namespace

void drawStatusBar(PanelContext& ctx)
{
    sampleSystem(g_stats);

    const float height = ImGui::GetFrameHeight() + 2.0f * dp(8.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding(), dp(8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::kBackground);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, height, flags)) {
        ImGui::AlignTextToFramePadding();

        stat("CPU", format("%.0f%%", g_stats.cpuPercent), g_stats.cpuPercent > 85.0 ? theme::kWarning : theme::kText);
        ImGui::SameLine(0.0f, dp(20.0f));
        stat("RAM", format("%.0f MB", g_stats.ramMb), theme::kText);
        ImGui::SameLine(0.0f, dp(20.0f));
        stat("VRAM", vramText(g_stats), theme::kText);
        ImGui::SameLine(0.0f, dp(20.0f));
        // verde = fluido, ambar = caindo frames
        const bool smooth = ctx.stats.frameMs <= 34.0f;
        stat("FPS", format("%.0f", static_cast<double>(ctx.stats.fps)), smooth ? theme::kSuccess : theme::kWarning);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%.1f ms/frame (cpu %.1f ms)\nOpenGL %s\n%s", static_cast<double>(ctx.stats.frameMs),
                              static_cast<double>(ctx.stats.workMs), ctx.glInfo.version.c_str(),
                              ctx.glInfo.gpu.c_str());
        }

        ImGui::SameLine(0.0f, dp(20.0f));
        ImGui::TextColored(theme::kTextDim, "Perfil");
        ImGui::SameLine(0.0f, dp(6.0f));
        ImGui::SetNextItemWidth(dp(130.0f));
        if (beginCombo("##perfil", settingsFor(ctx.profile).label)) {
            for (int i = 0; i < kProfileCount; ++i) {
                const auto p = static_cast<PerformanceProfile>(i);
                if (ImGui::Selectable(settingsFor(p).label, p == ctx.profile))
                    ctx.profile = p;
            }
            ImGui::EndCombo();
        }

        // Relógio à direita
        const std::time_t t = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &t);
        char clock[16];
        std::strftime(clock, sizeof(clock), "%H:%M", &local);
        ImFont* mono = fonts().mono;
        const float clockW = mono->CalcTextSizeA(fontPx(mono), FLT_MAX, 0.0f, clock).x;
        const float clockX = ImGui::GetWindowWidth() - padding() - clockW;

        // Aviso recente entre o perfil e o relógio (cortado se não couber)
        std::string warning = log::recentWarning();
        if (!warning.empty()) {
            ImGui::SameLine(0.0f, dp(20.0f));
            const float room = clockX - dp(16.0f) - ImGui::GetCursorPosX();
            while (!warning.empty() && ImGui::CalcTextSize(warning.c_str()).x > room) {
                warning.pop_back();
                // não corta no meio de um caractere UTF-8
                while (!warning.empty() && (static_cast<unsigned char>(warning.back()) & 0xC0) == 0x80)
                    warning.pop_back();
            }
            if (!warning.empty())
                ImGui::TextColored(theme::kWarning, "%s", warning.c_str());
        }

        ImGui::SameLine(clockX);
        ImGui::PushFont(mono, mono->LegacySize);
        ImGui::TextColored(theme::kTextDim, "%s", clock);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace bsl::ui
