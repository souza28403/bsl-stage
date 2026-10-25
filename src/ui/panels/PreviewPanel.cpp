#include "ui/panels/PreviewPanel.h"

#include "app/Paths.h"
#include "media/RemoteSource.h"
#include "render/Compositor.h"
#include "render/ThumbnailCache.h"
#include "ui/Brand.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"

#include <imgui.h>

#include <windows.h>

#include <algorithm>
#include <memory>
#include <string>

namespace bsl::ui {

namespace {

// Instância de fonte da Prévia: separada das camadas, então não interfere na saída
struct PreviewState
{
    std::unique_ptr<media::ISource> source;
    std::string key;     // identifica célula + clip da instância atual
    bool failed = false; // abrir falhou: não tenta de novo até a seleção mudar
    FitMode fit = FitMode::Fit;
    media::ParamValues params; // parametros ja entregues a instancia
};
PreviewState g_preview;

// Retângulo de destino e UVs para exibir um conteúdo cw x ch dentro de 'area'
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
        if (areaAspect > contentAspect) { // área mais larga: corta em cima e embaixo
            const float visible = contentAspect / areaAspect;
            p.uv0.y = (1.0f - visible) * 0.5f;
            p.uv1.y = 1.0f - p.uv0.y;
        } else { // área mais alta: corta nas laterais
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

// Web: com "Interagir" ligado, mouse e teclado sobre a imagem da Prévia vão para a página.
// As coordenadas são mapeadas do retângulo exibido (com o recorte do "Preencher") para pixels da página.
struct InteractState
{
    bool inside = false;
    int lastX = 0, lastY = 0;
    int held = 0; // botões do mouse que apertamos dentro da imagem (bit 0 = esquerdo, 1 = direito, 2 = meio)
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
    // Soltar o botão fora da imagem também chega à página (arrastos)
    for (int b = 0; b < 3; ++b) {
        if (ImGui::IsMouseReleased(b) && (st.held & (1 << b))) {
            int ux = st.lastX, uy = st.lastY;
            if (!inside)
                map(mp, ux, uy);
            web.sendMouse("up", ux, uy, b, 1, mods);
            st.held &= ~(1 << b);
        }
    }

    // Teclado: só com a Prévia em foco
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

} // namespace

void releasePreview()
{
    g_preview.source.reset();
    g_preview.key.clear();
    g_preview.failed = false;
}

void drawOutputPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dp(8.0f, 8.0f));
    beginPanel(panel::kOutput);

    const Compositor& canvas = ctx.canvas;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = static_cast<float>(canvas.width()) / static_cast<float>(canvas.height());

    // Reserva uma linha para a legenda e encaixa a imagem mantendo a proporcao
    const float caption = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 size(avail.x, avail.y - caption);
    if (size.x > 0.0f && size.y > 0.0f) {
        if (size.x / size.y > aspect)
            size.x = size.y * aspect;
        else
            size.y = size.x / aspect;

        // Centraliza
        ImVec2 cursor = ImGui::GetCursorPos();
        cursor.x += (avail.x - size.x) * 0.5f;
        cursor.y += (avail.y - caption - size.y) * 0.5f;
        ImGui::SetCursorPos(cursor);

        // O FBO tem origem embaixo e so a parte (0..uvMax) foi renderizada; meio texel de
        // folga evita sangrar a borda ao filtrar.
        const float s = canvas.uvMax();
        const float hx = 0.5f / static_cast<float>(canvas.width());
        const float hy = 0.5f / static_cast<float>(canvas.height());
        const ImVec2 uv0(hx, s - hy);
        const ImVec2 uv1(s - hx, hy);

        ImGui::Image(static_cast<ImTextureID>(canvas.texture()), size, uv0, uv1);
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Canvas vazio: monograma BSL a ~20% de opacidade, so como overlay da UI (a saida segue preta)
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

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x, ImGui::GetWindowHeight() - caption - dp(8.0f)));
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%dx%d  |  prévia a %.0f%%", canvas.width(), canvas.height(),
                           ctx.previewScale * 100.0f);
        popMono();
    }

    endPanel();
    ImGui::PopStyleVar();
}

void drawPreviewPanel(PanelContext& ctx)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dp(8.0f, 8.0f));
    beginPanel(panel::kPreview);

    Composition& comp = ctx.composition;
    const Selection& sel = ctx.selection;
    const Clip* clip = nullptr;
    if (sel.kind == Selection::Kind::Clip && comp.validCell(sel.layer, sel.column)
        && !comp.clip(sel.layer, sel.column).empty())
        clip = &comp.clip(sel.layer, sel.column);
    const bool eco = ctx.profile == PerformanceProfile::Eco;

    // A instância acompanha a seleção: troca de célula/clip fecha a anterior
    std::string key;
    if (clip)
        key = std::to_string(comp.currentDeck) + ":" + std::to_string(sel.layer) + ":" + std::to_string(sel.column)
              + ":" + std::to_string(static_cast<int>(clip->type)) + ":" + paths::toUtf8(clip->path) + ":"
              + clip->generatorId;
    if (key != g_preview.key) {
        g_preview.source.reset();
        g_preview.key = key;
        g_preview.failed = false;
    }
    // Fonte ao vivo (dispositivo/captura/web) e documentos (PDF/Office, processo auxiliar): exclusivos. A Prévia
    // nunca abre uma segunda instância; mostra a que está tocando na camada ou, se o clip estiver parado, a
    // miniatura (documento) ou um aviso com o nome.
    const bool live = clip && clip->isExclusive();
    const media::ISource* liveActive = (live && !eco && clip->isOpen() && clip->source->texture() != 0) ? clip->source.get() : nullptr;
    if (eco || live)
        g_preview.source.reset(); // Eco: só a miniatura, nada de decodificar
    else if (clip && !g_preview.source && !g_preview.failed) {
        g_preview.source = comp.openSource(*clip);
        g_preview.params = clip->params;
        if (g_preview.source)
            g_preview.source->play();
        else
            g_preview.failed = true;
    }
    if (g_preview.source && clip) {
        if (g_preview.params != clip->params) { // parametros do gerador editados no ClipPanel
            g_preview.params = clip->params;
            g_preview.source->setParams(clip->params);
        }
        // Velocidade e loop do clip valem tambem na prévia
        if (g_preview.source->loopMode() != clip->loop)
            g_preview.source->setLoopMode(clip->loop);
        if (g_preview.source->speed() != static_cast<double>(clip->speed))
            g_preview.source->setSpeed(clip->speed);
        g_preview.source->update(std::min(static_cast<double>(ImGui::GetIO().DeltaTime), 0.1));
    }

    // --- Área de imagem (o rodapé fica com uma linha de controles) ---
    const float footerH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 areaSize(std::max(avail.x, 8.0f), std::max(avail.y - footerH, 8.0f));
    const ImVec2 areaMin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(areaSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 areaMax(areaMin.x + areaSize.x, areaMin.y + areaSize.y);
    dl->AddRectFilled(areaMin, areaMax, ImGui::GetColorU32(theme::kBackground), rounding());

    if (!clip) {
        const char* text = "Selecione um clip na grade";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2((areaMin.x + areaMax.x - ts.x) * 0.5f, (areaMin.y + areaMax.y - ts.y) * 0.5f),
                    ImGui::GetColorU32(theme::kTextDim), text);
    } else if (g_preview.source || liveActive) {
        const media::ISource& src = g_preview.source ? *g_preview.source : *liveActive;
        const Placement p = place(g_preview.fit, areaMin, areaSize, static_cast<float>(src.width()),
                                  static_cast<float>(src.height()));
        drawCheckerboard(dl, p.a, p.b);
        if (src.texture() != 0)
            dl->AddImage(static_cast<ImTextureID>(src.texture()), p.a, p.b, p.uv0, p.uv1);
        const bool interacting = liveActive && clip->interact && clip->isWeb();
        dl->AddRect(p.a, p.b, interacting ? ImGui::GetColorU32(theme::kAccent) : ImGui::GetColorU32(theme::kBorder));
        if (interacting) {
            if (auto* web = dynamic_cast<media::RemoteSource*>(clip->source.get()))
                forwardInteraction(*web, p, src.width(), src.height());
            const char* tag = "Interagindo com a página";
            const ImVec2 ts = ImGui::CalcTextSize(tag);
            dl->AddRectFilled(ImVec2(p.a.x + dp(6.0f), p.a.y + dp(6.0f)),
                              ImVec2(p.a.x + dp(14.0f) + ts.x, p.a.y + dp(12.0f) + ts.y), IM_COL32(0, 0, 0, 170), dp(3.0f));
            dl->AddText(ImVec2(p.a.x + dp(10.0f), p.a.y + dp(9.0f)), ImGui::GetColorU32(theme::kText), tag);
        } else {
            g_interact = InteractState{};
        }
    } else {
        // Eco (ou a fonte não abriu): só a miniatura
        const Placement p = place(g_preview.fit, areaMin, areaSize, 160.0f, 90.0f);
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
            if (!clip->liveThumb.id || live) {
                dl->AddText(bold, fontPx(bold),
                            ImVec2((areaMin.x + areaMax.x - ts.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f - ts.y),
                            ImGui::GetColorU32(theme::kText), title.c_str());
                const char* hint = !live ? "Dispare o clip para ver a prévia"
                                         : (eco ? "Perfil Eco: sem imagem ao vivo na prévia"
                                                : "Fonte ao vivo: dispare o clip para ver a imagem");
                if (clip->isWeb() && !eco)
                    hint = "Página web: dispare o clip para ver a imagem";
                const ImVec2 hs = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2((areaMin.x + areaMax.x - hs.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f + dp(4.0f)),
                            ImGui::GetColorU32(theme::kTextDim), hint);
            }
        } else if (thumb) {
            drawCheckerboard(dl, p.a, p.b);
            dl->AddImage(static_cast<ImTextureID>(thumb), p.a, p.b, p.uv0, p.uv1);
        } else {
            drawCenteredMono(dl, areaMin, areaMax, "...", ImGui::GetColorU32(theme::kTextDim));
        }
        dl->AddRect(p.a, p.b, ImGui::GetColorU32(theme::kBorder));
    }

    // --- Rodapé: situação + Ajustar/Preencher ---
    ImGui::AlignTextToFramePadding();
    if (clip && eco)
        ImGui::TextColored(theme::kTextDim, "Perfil Eco: só a miniatura");
    else if (clip && g_preview.failed)
        ImGui::TextColored(theme::kWarning, "Não foi possível abrir o clip");
    else if (clip)
        ImGui::TextColored(theme::kTextDim, "%s", clip->name.c_str());
    else
        ImGui::TextColored(theme::kTextDim, "Sem clip");

    const float comboW = dp(110.0f);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - comboW - dp(8.0f)));
    ImGui::SetNextItemWidth(comboW);
    if (beginCombo("##previaEncaixe", fitModeName(g_preview.fit))) {
        for (FitMode m : {FitMode::Fit, FitMode::Fill})
            if (ImGui::Selectable(fitModeName(m), m == g_preview.fit))
                g_preview.fit = m;
        ImGui::EndCombo();
    }

    endPanel();
    ImGui::PopStyleVar();
}

} // namespace bsl::ui
