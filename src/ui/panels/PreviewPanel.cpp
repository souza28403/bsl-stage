#include "ui/panels/PreviewPanel.h"

#include "app/Paths.h"
#include "app/Qa.h"
#include "audio/AudioAnalyzer.h"
#include "fx/ChainRunner.h"
#include "media/RemoteSource.h"
#include "render/Compositor.h"
#include "render/ThumbnailCache.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/TransformEditor.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace bsl::ui {

namespace {

struct PreviewState
{
    std::unique_ptr<media::ISource> source;
    std::string key;
    bool failed = false;
    media::ParamValues params;
    fx::EffectChain chain;
    FboPool pool;
    FboPool::Lease lease;
    std::uint64_t frame = 0;
};
PreviewState g_preview;
PreviewOptions g_options;
int g_wantedLayerId = 0;
PreviewInfo g_info;

GLuint previewWithEffects(const PanelContext& ctx, const Clip& clip, const media::ISource& src)
{
    g_preview.lease.reset();
    if (!clip.effects.hasActive() || src.texture() == 0 || src.width() <= 0 || src.height() <= 0) {
        if (clip.effects.empty())
            g_preview.chain.items.clear();
        return src.texture();
    }
    g_preview.chain.syncFrom(clip.effects);
    const Composition& comp = ctx.composition;
    fx::EffectContext fxc;
    fxc.time = ImGui::GetTime();
    fxc.dt = std::min(static_cast<double>(ImGui::GetIO().DeltaTime), 0.1);
    fxc.frameIndex = g_preview.frame++;
    fxc.bpm = comp.bpm;
    fxc.beat = comp.beat;
    fxc.beatPhase = static_cast<float>(comp.beat - std::floor(comp.beat));
    fxc.barPhase = static_cast<float>(std::fmod(std::max(comp.beat, 0.0), 4.0) / 4.0);
    fxc.canvasWidth = comp.width;
    fxc.canvasHeight = comp.height;
    fxc.topOrigin = true;
    fxc.preview = true;
    fxc.sourceId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&src));
    fxc.pool = &g_preview.pool;
    fxc.audio = audio::AudioAnalyzer::instance().snapshot();
    g_preview.lease = fx::ChainRunner::run(g_preview.chain, fxc, src.texture(), src.width(), src.height());
    g_preview.pool.endFrame();
    return g_preview.lease ? g_preview.lease->texture() : src.texture();
}

struct Placement
{
    ImVec2 a, b;
    ImVec2 uv0{0.0f, 0.0f}, uv1{1.0f, 1.0f};
};

Placement place(FitMode mode, ImVec2 areaMin, ImVec2 areaSize, float cw, float ch)
{
    Placement p;
    const float contentAspect = cw / std::max(ch, 1.0f);
    const float areaAspect = areaSize.x / std::max(areaSize.y, 1.0f);
    if (mode == FitMode::Fill) {
        p.a = areaMin;
        p.b = ImVec2(areaMin.x + areaSize.x, areaMin.y + areaSize.y);
        if (areaAspect > contentAspect) {
            const float visible = contentAspect / areaAspect;
            p.uv0.y = (1.0f - visible) * 0.5f;
            p.uv1.y = 1.0f - p.uv0.y;
        } else {
            const float visible = areaAspect / contentAspect;
            p.uv0.x = (1.0f - visible) * 0.5f;
            p.uv1.x = 1.0f - p.uv0.x;
        }
    } else {
        ImVec2 size = areaSize;
        if (areaAspect > contentAspect)
            size.x = size.y * contentAspect;
        else
            size.y = size.x / contentAspect;
        p.a = ImVec2(areaMin.x + (areaSize.x - size.x) * 0.5f, areaMin.y + (areaSize.y - size.y) * 0.5f);
        p.b = ImVec2(p.a.x + size.x, p.a.y + size.y);
    }
    return p;
}

struct InteractState
{
    bool inside = false;
    int lastX = 0, lastY = 0;
    int held = 0;
};
InteractState g_interact;

void forwardInteraction(media::RemoteSource& web, const Placement& p, int contentW, int contentH)
{
    if (contentW <= 0 || contentH <= 0 || p.b.x <= p.a.x || p.b.y <= p.a.y)
        return;
    ImGuiIO& io = ImGui::GetIO();
    auto map = [&](ImVec2 m, int& x, int& y) {
        const float fx = std::clamp((m.x - p.a.x) / (p.b.x - p.a.x), 0.0f, 1.0f);
        const float fy = std::clamp((m.y - p.a.y) / (p.b.y - p.a.y), 0.0f, 1.0f);
        const float u = p.uv0.x + fx * (p.uv1.x - p.uv0.x);
        const float v = p.uv0.y + fy * (p.uv1.y - p.uv0.y);
        x = std::clamp(static_cast<int>(u * static_cast<float>(contentW)), 0, contentW - 1);
        y = std::clamp(static_cast<int>(v * static_cast<float>(contentH)), 0, contentH - 1);
    };
    const int mods = (io.KeyShift ? 1 : 0) | (io.KeyCtrl ? 2 : 0) | (io.KeyAlt ? 4 : 0);
    const ImVec2 mp = io.MousePos;
    const bool inside = ImGui::IsWindowHovered() && mp.x >= p.a.x && mp.x < p.b.x && mp.y >= p.a.y && mp.y < p.b.y;
    InteractState& st = g_interact;

    int x = st.lastX, y = st.lastY;
    if (inside) {
        map(mp, x, y);
        if (!st.inside || x != st.lastX || y != st.lastY)
            web.sendMouse("move", x, y, st.held, 0, mods);
        st.inside = true;
        for (int b = 0; b < 3; ++b) {
            if (ImGui::IsMouseClicked(b)) {
                ImGui::SetWindowFocus();
                web.sendMouse("down", x, y, b, std::max(1, static_cast<int>(io.MouseClickedCount[b])), mods);
                st.held |= 1 << b;
            }
        }
        if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)
            web.sendWheel(x, y, io.MouseWheelH * 100.0f, io.MouseWheel * 100.0f);
    } else if (st.inside) {
        web.sendMouse("leave", st.lastX, st.lastY, 0, 0, mods);
        st.inside = false;
    }
    st.lastX = x;
    st.lastY = y;
    for (int b = 0; b < 3; ++b) {
        if (ImGui::IsMouseReleased(b) && (st.held & (1 << b))) {
            int ux = st.lastX, uy = st.lastY;
            if (!inside)
                map(mp, ux, uy);
            web.sendMouse("up", ux, uy, b, 1, mods);
            st.held &= ~(1 << b);
        }
    }

    if (ImGui::IsWindowFocused()) {
        struct KeyMap
        {
            ImGuiKey key;
            int vk;
        };
        static const KeyMap kKeys[] = {
            {ImGuiKey_Enter, VK_RETURN}, {ImGuiKey_KeypadEnter, VK_RETURN}, {ImGuiKey_Backspace, VK_BACK},
            {ImGuiKey_Tab, VK_TAB},      {ImGuiKey_Escape, VK_ESCAPE},      {ImGuiKey_LeftArrow, VK_LEFT},
            {ImGuiKey_RightArrow, VK_RIGHT}, {ImGuiKey_UpArrow, VK_UP},     {ImGuiKey_DownArrow, VK_DOWN},
            {ImGuiKey_Home, VK_HOME},    {ImGuiKey_End, VK_END},            {ImGuiKey_PageUp, VK_PRIOR},
            {ImGuiKey_PageDown, VK_NEXT}, {ImGuiKey_Delete, VK_DELETE},     {ImGuiKey_Insert, VK_INSERT},
            {ImGuiKey_Space, VK_SPACE}};
        for (const KeyMap& k : kKeys) {
            if (ImGui::IsKeyPressed(k.key, true))
                web.sendKey("down", k.vk, 0, mods);
            if (ImGui::IsKeyReleased(k.key))
                web.sendKey("up", k.vk, 0, mods);
        }
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            const unsigned c = static_cast<unsigned>(io.InputQueueCharacters[i]);
            if (c >= 32)
                web.sendKey("char", 0, c, mods);
        }
    }
}

}

PreviewOptions& previewOptions()
{
    return g_options;
}

PreviewInfo previewInfo()
{
    return g_info;
}

void releasePreview()
{
    g_wantedLayerId = 0;
    g_preview.lease.reset();
    g_preview.pool.clear();
    g_preview.chain.items.clear();
    g_preview.source.reset();
    g_preview.key.clear();
    g_preview.failed = false;
}

void drawOutputPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dp(8.0f, 8.0f));
    beginPanel(panel::kOutput);

    const Compositor& canvas = ctx.canvas;

    transformToggleButton(TransformPanel::Output, ImVec2(0.0f, 0.0f));
    const bool editing = transformEditOn(TransformPanel::Output);
    if (editing)
        drawTransformTools(ctx, TransformPanel::Output);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = static_cast<float>(canvas.width()) / static_cast<float>(canvas.height());

    const float caption = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 size(avail.x, avail.y - caption);
    if (size.x > 0.0f && size.y > 0.0f) {
        if (size.x / size.y > aspect)
            size.x = size.y * aspect;
        else
            size.y = size.x / aspect;

        ImVec2 cursor = ImGui::GetCursorPos();
        cursor.x += (avail.x - size.x) * 0.5f;
        cursor.y += (avail.y - caption - size.y) * 0.5f;
        ImGui::SetCursorPos(cursor);

        const float s = canvas.uvMax();
        const float hx = 0.5f / static_cast<float>(canvas.width());
        const float hy = 0.5f / static_cast<float>(canvas.height());
        const ImVec2 uv0(hx, s - hy);
        const ImVec2 uv1(s - hx, hy);

        ImGui::Image(static_cast<ImTextureID>(canvas.texture()), size, uv0, uv1);
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const Texture& mark = brand().monogram;
        if (!ctx.composition.anyActive() && mark) {
            const float w = size.x * 0.30f;
            const float h = w * static_cast<float>(mark.height) / static_cast<float>(mark.width);
            const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            dl->AddImage(static_cast<ImTextureID>(mark.id), ImVec2(c.x - w * 0.5f, c.y - h * 0.5f),
                         ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
                         IM_COL32(255, 255, 255, 51));
        }
        dl->AddRect(a, b, ImGui::GetColorU32(theme::kBorder));
        if (editing)
            drawTransformOverlay(ctx, TransformPanel::Output, a, b);

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x, ImGui::GetWindowHeight() - caption - dp(8.0f)));
        pushMono();
        const std::string transformInfo = editing ? transformCaption(ctx) : std::string();
        if (!transformInfo.empty())
            ImGui::TextColored(theme::kTextDim, "%s", transformInfo.c_str());
        else
            ImGui::TextColored(theme::kTextDim, "%dx%d  |  prévia a %.0f%%", canvas.width(), canvas.height(),
                               ctx.previewScale * 100.0f);
        popMono();
    }

    endPanel();
    ImGui::PopStyleVar();
}

namespace {

const char* sourceLabel(PreviewSource s)
{
    return s == PreviewSource::Layer ? "Camada selecionada" : "Clip selecionado";
}

void drawBadge(ImDrawList* dl, ImVec2 pos, const char* tag, bool hot, const std::string& name, float maxRight)
{
    ImFont* bold = fonts().bold;
    const float px = fontPx(bold) * 0.82f;
    const ImVec2 ts = bold->CalcTextSizeA(px, FLT_MAX, 0.0f, tag);
    const float padX = dp(6.0f), padY = dp(2.0f);
    const ImVec2 a = pos;
    const ImVec2 b(a.x + ts.x + padX * 2.0f, a.y + ts.y + padY * 2.0f);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(hot ? theme::kAccent : theme::kPanelActive), dp(3.0f));
    dl->AddText(bold, px, ImVec2(a.x + padX, a.y + padY), ImGui::GetColorU32(hot ? theme::kOnAccent : theme::kText), tag);
    if (name.empty())
        return;
    const float nameX = b.x + dp(6.0f);
    if (nameX >= maxRight)
        return;
    dl->PushClipRect(ImVec2(nameX, a.y), ImVec2(maxRight, b.y), true);
    const ImVec2 ns = ImGui::CalcTextSize(name.c_str());
    dl->AddRectFilled(ImVec2(nameX - dp(4.0f), a.y), ImVec2(std::min(nameX + ns.x + dp(4.0f), maxRight), b.y),
                      IM_COL32(0, 0, 0, 150), dp(3.0f));
    dl->AddText(ImVec2(nameX, a.y + (b.y - a.y - ns.y) * 0.5f), ImGui::GetColorU32(theme::kText), name.c_str());
    dl->PopClipRect();
}

void drawPreviewTransformMode(PanelContext& ctx)
{
    const Compositor& canvas = ctx.canvas;
    transformToggleButton(TransformPanel::Preview, ImVec2(0.0f, 0.0f));
    drawTransformTools(ctx, TransformPanel::Preview);

    const float footerH = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 areaSize(std::max(avail.x, 8.0f), std::max(avail.y - footerH, 8.0f));
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(areaMin, ImVec2(areaMin.x + areaSize.x, areaMin.y + areaSize.y), ImGui::GetColorU32(theme::kBackground),
                      rounding());
    const Placement p = place(FitMode::Fit, areaMin, areaSize, static_cast<float>(canvas.width()),
                              static_cast<float>(canvas.height()));
    qa::noteRect("previa:area", areaMin.x, areaMin.y, areaMin.x + areaSize.x, areaMin.y + areaSize.y);
    const float s = canvas.uvMax();
    const float hx = 0.5f / static_cast<float>(canvas.width()), hy = 0.5f / static_cast<float>(canvas.height());
    dl->AddImage(static_cast<ImTextureID>(canvas.texture()), p.a, p.b, ImVec2(hx, s - hy), ImVec2(s - hx, hy));
    dl->AddRect(p.a, p.b, ImGui::GetColorU32(theme::kBorder));
    drawTransformOverlay(ctx, TransformPanel::Preview, p.a, p.b);

    ImGui::SetCursorScreenPos(ImVec2(areaMin.x, areaMin.y + areaSize.y + ImGui::GetStyle().ItemSpacing.y));
    pushMono();
    const std::string info = transformCaption(ctx);
    ImGui::TextColored(theme::kTextDim, "%s", info.empty() ? "Transformar: clique numa camada" : info.c_str());
    popMono();
}

}

int previewWantedLayerId()
{
    return g_wantedLayerId;
}

void previewPanelHidden()
{
    g_preview.source.reset();
    g_preview.key.clear();
    g_wantedLayerId = 0;
    g_info = {};
}

void drawPreviewPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dp(8.0f, 8.0f));
    beginPanel(panel::kPreview);

    if (transformEditOn(TransformPanel::Preview)) {
        g_preview.source.reset();
        g_preview.key.clear();
        g_wantedLayerId = 0;
        g_info = {};
        g_info.layerMode = g_options.source == PreviewSource::Layer;
        drawPreviewTransformMode(ctx);
        endPanel();
        ImGui::PopStyleVar();
        return;
    }

    Composition& comp = ctx.composition;
    const Selection& sel = ctx.selection;
    PreviewOptions& opt = g_options;
    const bool eco = ctx.profile == PerformanceProfile::Eco;
    const bool layerMode = opt.source == PreviewSource::Layer;

    int layerIndex = -1;
    if (sel.kind != Selection::Kind::None && sel.layer >= 0 && sel.layer < comp.layerCount())
        layerIndex = sel.layer;
    Clip* clip = nullptr;
    int cellLayer = -1, cellColumn = -1;
    if (layerMode) {
        if (layerIndex >= 0)
            clip = comp.activeClip(layerIndex);
    } else if (sel.kind == Selection::Kind::Clip && comp.validCell(sel.layer, sel.column)
               && !comp.clip(sel.layer, sel.column).empty()) {
        clip = &comp.clip(sel.layer, sel.column);
        cellLayer = sel.layer;
        cellColumn = sel.column;
    }
    const bool live = clip && clip->isOpen();

    std::string key;
    if (clip && !layerMode)
        key = std::to_string(comp.currentDeck) + ":" + std::to_string(sel.layer) + ":" + std::to_string(sel.column)
              + ":" + std::to_string(static_cast<int>(clip->type)) + ":" + paths::toUtf8(clip->path) + ":"
              + clip->generatorId;
    if (key != g_preview.key) {
        g_preview.source.reset();
        g_preview.key = key;
        g_preview.failed = false;
    }
    const bool wantOwn = clip && !layerMode && !eco && !clip->isExclusive() && !live;
    if (!wantOwn) {
        g_preview.source.reset();
    } else if (!g_preview.source && !g_preview.failed) {
        g_preview.source = comp.openSource(*clip);
        g_preview.params = clip->params;
        if (g_preview.source)
            g_preview.source->play();
        else
            g_preview.failed = true;
    }
    if (g_preview.source && clip) {
        if (g_preview.params != clip->params) {
            g_preview.params = clip->params;
            g_preview.source->setParams(clip->params);
        }
        if (g_preview.source->loopMode() != clip->loop)
            g_preview.source->setLoopMode(clip->loop);
        if (g_preview.source->speed() != static_cast<double>(clip->speed))
            g_preview.source->setSpeed(clip->speed);
        g_preview.source->update(std::min(static_cast<double>(ImGui::GetIO().DeltaTime), 0.1));
    }
    const media::ISource* mirrorSrc = clip ? (live ? clip->source.get() : clip->parked.get()) : nullptr;
    const media::ISource* mirror = (mirrorSrc && !eco && mirrorSrc->texture() != 0) ? mirrorSrc : nullptr;
    const media::ISource* shown = g_preview.source ? g_preview.source.get() : mirror;

    bool sendLive = false;
    {
        const float btnW = ImGui::CalcTextSize(ICON_PLAY "  Ao vivo").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float room = ImGui::GetContentRegionAvail().x - btnW - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x * 2.0f;
        ImGui::SetNextItemWidth(std::clamp(room, dp(70.0f), dp(172.0f)));
    }
    if (beginCombo("##previa_fonte", sourceLabel(opt.source))) {
        for (PreviewSource s : {PreviewSource::Clip, PreviewSource::Layer})
            if (ImGui::Selectable(sourceLabel(s), s == opt.source))
                opt.source = s;
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("O que a Prévia mostra: o clip selecionado ou o que a camada selecionada está tocando");
    ImGui::SameLine();
    const bool canSend = clip && !layerMode && !clip->missing && !live;
    ImGui::BeginDisabled(!canSend);
    if (canSend) {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::kAccentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::kAccentDim);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kOnAccent);
    }
    if (ImGui::Button(ICON_PLAY "  Ao vivo##previa_ao_vivo"))
        sendLive = true;
    if (canSend)
        ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", layerMode ? "Escolha \"Clip selecionado\" para enviar um clip para a saída"
                                : live    ? "Este clip já está na saída"
                                : clip    ? "Enviar para saída: dispara este clip na camada dele"
                                          : "Selecione um clip na grade");
    ImGui::SameLine();
    transformToggleButton(TransformPanel::Preview, ImVec2(0.0f, 0.0f));

    const float footerH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 areaSize(std::max(avail.x, 8.0f), std::max(avail.y - footerH, 8.0f));
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(areaSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 areaMax(areaMin.x + areaSize.x, areaMin.y + areaSize.y);
    dl->AddRectFilled(areaMin, areaMax, ImGui::GetColorU32(theme::kBackground), rounding());
    qa::noteRect("previa:area", areaMin.x, areaMin.y, areaMax.x, areaMax.y);

    const bool sliceOnlyLayer = layerMode && layerIndex >= 0 && comp.layers[static_cast<size_t>(layerIndex)].sliceOnly;
    const std::string badgeTag = layerMode ? "CAMADA " + std::to_string(layerIndex + 1) + (sliceOnlyLayer ? " · SLICE" : "")
                                           : (live ? "AO VIVO" : "PRÉVIA");
    const bool badgeHot = live;
    g_wantedLayerId = (layerMode && layerIndex >= 0) ? comp.layers[static_cast<size_t>(layerIndex)].id : 0;
    const ImVec2 badgePos(areaMin.x + dp(6.0f), areaMin.y + dp(6.0f));

    if (!clip) {
        const char* text = !layerMode        ? "Selecione um clip na grade"
                           : layerIndex < 0  ? "Selecione uma camada"
                                             : "Nenhum clip tocando nesta camada";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2((areaMin.x + areaMax.x - ts.x) * 0.5f, (areaMin.y + areaMax.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(theme::kTextDim), text);
        if (layerMode && layerIndex >= 0)
            drawBadge(dl, badgePos, badgeTag.c_str(), false, comp.layers[static_cast<size_t>(layerIndex)].name, areaMax.x - dp(6.0f));
    } else if (layerMode) {
        const Layer& layer = comp.layers[static_cast<size_t>(layerIndex)];
        const Placement p = place(opt.fit, areaMin, areaSize, static_cast<float>(comp.width), static_cast<float>(comp.height));
        dl->AddRectFilled(p.a, p.b, IM_COL32(0, 0, 0, 255));
        GLuint viewTex = 0;
        if (ctx.canvas.layerView(layer.id, false, viewTex, true) && viewTex != 0) {
            const float us = ctx.canvas.uvMax();
            const float hx = 0.5f / static_cast<float>(ctx.canvas.width()), hy = 0.5f / static_cast<float>(ctx.canvas.height());
            const ImVec2 uv0(hx + p.uv0.x * (us - 2.0f * hx), (us - hy) - p.uv0.y * (us - 2.0f * hy));
            const ImVec2 uv1(hx + p.uv1.x * (us - 2.0f * hx), (us - hy) - p.uv1.y * (us - 2.0f * hy));
            dl->AddImage(static_cast<ImTextureID>(viewTex), p.a, p.b, uv0, uv1);
        }
        dl->AddRect(p.a, p.b, ImGui::GetColorU32(theme::kBorder));
        g_interact = InteractState{};
        drawBadge(dl, badgePos, badgeTag.c_str(), badgeHot, clip->name, areaMax.x - dp(6.0f));
    } else if (shown) {
        const media::ISource& src = *shown;
        const GLuint tex = previewWithEffects(ctx, *clip, src);
        const Placement p = place(opt.fit, areaMin, areaSize, static_cast<float>(src.width()), static_cast<float>(src.height()));
        if (opt.checker)
            drawCheckerboard(dl, p.a, p.b);
        else
            dl->AddRectFilled(p.a, p.b, IM_COL32(0, 0, 0, 255));
        if (tex != 0)
            dl->AddImage(static_cast<ImTextureID>(tex), p.a, p.b, p.uv0, p.uv1);
        const bool interacting = mirror && clip->interact && clip->isWeb();
        dl->AddRect(p.a, p.b, interacting ? ImGui::GetColorU32(theme::kAccent) : ImGui::GetColorU32(theme::kBorder));
        if (interacting) {
            if (auto* web = dynamic_cast<media::RemoteSource*>(clip->liveSource()))
                forwardInteraction(*web, p, src.width(), src.height());
            const char* tag = "Interagindo com a página";
            const ImVec2 ts = ImGui::CalcTextSize(tag);
            dl->AddRectFilled(ImVec2(p.a.x + dp(6.0f), p.a.y + dp(6.0f)),
                              ImVec2(p.a.x + dp(14.0f) + ts.x, p.a.y + dp(12.0f) + ts.y), IM_COL32(0, 0, 0, 170), dp(3.0f));
            dl->AddText(ImVec2(p.a.x + dp(10.0f), p.a.y + dp(9.0f)), ImGui::GetColorU32(theme::kText), tag);
        } else {
            g_interact = InteractState{};
            drawBadge(dl, badgePos, badgeTag.c_str(), badgeHot, clip->name, areaMax.x - dp(6.0f));
        }
    } else {
        const bool live2 = clip->isExclusive();
        const Placement p = place(opt.fit, areaMin, areaSize, 160.0f, 90.0f);
        const GLuint thumb = (clip->type == Clip::Type::File && !clip->isWeb()) ? ctx.thumbs.get(clip->path) : 0;
        if (clip->isTestPattern()) {
            drawPatternThumb(dl, p.a, p.b);
        } else if (clip->isGenerator() || clip->isWeb()) {
            drawCheckerboard(dl, p.a, p.b);
            if (clip->liveThumb.id)
                dl->AddImage(static_cast<ImTextureID>(clip->liveThumb.id), p.a, p.b);
            const GeneratorInfo* info = clip->generatorInfo();
            const std::string title = info ? info->name : "Página web";
            ImFont* bold = fonts().bold;
            const ImVec2 ts = bold->CalcTextSizeA(fontPx(bold), FLT_MAX, 0.0f, title.c_str());
            if (!clip->liveThumb.id || live2) {
                dl->AddText(bold, fontPx(bold),
                            ImVec2((areaMin.x + areaMax.x - ts.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f - ts.y),
                            ImGui::GetColorU32(theme::kText), title.c_str());
                const char* hint = !live2 ? "Dispare o clip para ver a prévia"
                                          : (eco ? "Perfil Eco: sem imagem ao vivo na prévia"
                                                 : "Fonte ao vivo: dispare o clip para ver a imagem");
                if (clip->isWeb() && !eco)
                    hint = "Página web: dispare o clip para ver a imagem";
                const ImVec2 hs = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2((areaMin.x + areaMax.x - hs.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f + dp(4.0f)),
                            ImGui::GetColorU32(theme::kTextDim), hint);
            }
        } else if (thumb) {
            if (opt.checker)
                drawCheckerboard(dl, p.a, p.b);
            dl->AddImage(static_cast<ImTextureID>(thumb), p.a, p.b, p.uv0, p.uv1);
        } else {
            drawCenteredMono(dl, areaMin, areaMax, "...", ImGui::GetColorU32(theme::kTextDim));
        }
        dl->AddRect(p.a, p.b, ImGui::GetColorU32(theme::kBorder));
        drawBadge(dl, badgePos, badgeTag.c_str(), badgeHot, clip->name, areaMax.x - dp(6.0f));
        const char* note = eco ? "Perfil Eco: só a miniatura"
                           : g_preview.failed ? "Não foi possível abrir o clip"
                                              : nullptr;
        if (note) {
            const ImVec2 ts = ImGui::CalcTextSize(note);
            dl->AddText(ImVec2((areaMin.x + areaMax.x - ts.x) * 0.5f, areaMax.y - ts.y - dp(6.0f)),
                        ImGui::GetColorU32(g_preview.failed ? theme::kWarning : theme::kTextDim), note);
        }
    }

    media::ISource* own = g_preview.source.get();
    media::ISource* liveSrc = (!own && live && !clip->isLive() && !clip->hasPageNav()) ? clip->source.get() : nullptr;
    media::ISource* clock = own ? own : liveSrc;
    const bool hasTransport = clock && clock->duration() > 0.0;
    const bool playing = own ? own->isPlaying() : (liveSrc ? clip->playing : false);
    const double duration = clock ? clock->duration() : 0.0;
    float pos = clock ? static_cast<float>(clock->position()) : 0.0f;

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float frameH = ImGui::GetFrameHeight();
    const float playW = dp(32.0f), checkerW = dp(28.0f), comboW = dp(100.0f);
    const char* playTip = live ? (playing ? "Pausar (clip ao vivo: pausa a saída)" : "Tocar (clip ao vivo: toca a saída)")
                               : (playing ? "Pausar a prévia (a saída não muda)" : "Tocar a prévia (a saída não muda)");
    ImGui::BeginDisabled(!hasTransport);
    if (iconButton(playing ? ICON_PAUSE : ICON_PLAY, "##previa_tocar_pausar", playTip, ImVec2(playW, frameH),
                   playing ? IconButton_Active : 0)) {
        if (own) {
            if (playing)
                own->pause();
            else
                own->play();
        } else if (liveSrc) {
            clip->setPlaying(!clip->playing);
        }
    }
    ImGui::SameLine();
    const float sliderW = std::max(dp(40.0f), ImGui::GetContentRegionAvail().x - checkerW - comboW - 2.0f * spacing);
    const std::string posLabel = formatTime(pos) + " / " + formatTime(duration);
    ImGui::SetNextItemWidth(sliderW);
    pushMono();
    if (ImGui::SliderFloat("##previa_posicao", &pos, 0.0f, static_cast<float>(duration > 0.0 ? duration : 1.0),
                           posLabel.c_str(), ImGuiSliderFlags_NoRoundToFormat)) {
        if (own)
            own->seek(pos);
        else if (liveSrc)
            clip->seek(pos);
    }
    popMono();
    ImGui::EndDisabled();
    ImGui::SameLine();
    iconToggle(ICON_GRID_2X2, "##previa_quadriculado", opt.checker, "Quadriculado atrás do alfa", ImVec2(checkerW, frameH));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboW);
    if (beginCombo("##previaEncaixe", fitModeName(opt.fit))) {
        for (FitMode m : {FitMode::Fit, FitMode::Fill})
            if (ImGui::Selectable(fitModeName(m), m == opt.fit))
                opt.fit = m;
        ImGui::EndCombo();
    }

    g_info = {};
    g_info.layerMode = layerMode;
    g_info.layer = layerMode ? layerIndex : cellLayer;
    g_info.column = layerMode ? (clip ? comp.layers[static_cast<size_t>(layerIndex)].activeColumn : -1) : cellColumn;
    g_info.hasClip = clip != nullptr;
    g_info.live = live;
    g_info.ownSource = own != nullptr;
    g_info.playing = playing;
    g_info.position = static_cast<double>(pos);
    g_info.duration = duration;

    endPanel();
    ImGui::PopStyleVar();

    if (sendLive && cellLayer >= 0)
        comp.trigger(cellLayer, cellColumn);
}

}
