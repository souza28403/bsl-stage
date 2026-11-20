#include "ui/panels/ShowCacheWindow.h"

#include "app/Paths.h"
#include "app/ShowCacheQueue.h"
#include "io/FileDialogs.h"
#include "media/ShowCache.h"
#include "platform/WindowHandle.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <cstdio>
#include <set>
#include <string>

namespace bsl::ui {

namespace {

struct WindowState
{
    bool open = false;
    std::string message;
};
WindowState g_window;

void applyFolder(PanelContext& ctx, const std::string& utf8)
{
    ctx.settings.showCacheDir = utf8;
    ctx.settings.save();
    ctx.showCache.setCacheDirOverride(utf8);
    ctx.composition.showCacheDir = ctx.showCache.cacheDir();
}

std::set<std::string> keysInUse(const Composition& comp)
{
    std::set<std::string> keys;
    for (const Deck& deck : comp.decks)
        for (const auto& row : deck.clips)
            for (const Clip& c : row) {
                if (!c.showCache || c.type != Clip::Type::File || c.kind != media::MediaKind::Video)
                    continue;
                const auto [w, h] = comp.showCacheLimits(c);
                const std::string key = media::showCacheKey(c.path, w, h);
                if (!key.empty())
                    keys.insert(key);
            }
    return keys;
}

}

void openShowCacheWindow()
{
    g_window.open = true;
    g_window.message.clear();
}

void drawShowCacheWindow(PanelContext& ctx)
{
    if (!g_window.open)
        return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(dp(520.0f), dp(300.0f)), ImGuiCond_Appearing);
    if (!ImGui::Begin("Cache de show###janela_cache_show", &g_window.open,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim,
                       "Pasta onde ficam as versões otimizadas (HAP) dos vídeos. Os originais nunca são alterados.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    ImGui::SeparatorText("Pasta do cache");
    pushMono();
    ImGui::TextWrapped("%s", paths::toUtf8(ctx.showCache.cacheDir()).c_str());
    popMono();
    ImGui::Spacing();
    if (ImGui::Button("Escolher pasta…##cache_show_escolher")) {
        if (auto dir = dialogs::pickFolder(bslplat::activeWindowHandle(), ctx.showCache.cacheDir()))
            applyFolder(ctx, paths::toUtf8(*dir));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(ctx.settings.showCacheDir.empty());
    if (ImGui::Button("Usar padrão##cache_show_padrao"))
        applyFolder(ctx, std::string());
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SeparatorText("Conversões");
    bool paused = ctx.showCache.paused();
    if (ImGui::Checkbox("Pausar conversões##cache_show_pausar", &paused))
        ctx.showCache.setPaused(paused);

    ImGui::Spacing();
    if (ImGui::Button("Limpar cache não usado##cache_show_limpar")) {
        const std::uintmax_t freed = media::clearShowCache(ctx.showCache.cacheDir(), keysInUse(ctx.composition));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Liberados %.1f MB", static_cast<double>(freed) / (1024.0 * 1024.0));
        g_window.message = buf;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Apaga as versões otimizadas que nenhum clip do projeto aberto usa.");
    if (!g_window.message.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "%s", g_window.message.c_str());
    }

    ImGui::End();
}

}
