#include "ui/panels/ClipPanel.h"

#include "app/Paths.h"
#include "media/CaptureTargets.h"
#include "media/DeviceCatalog.h"
#include "media/DocInfo.h"
#include "media/ImageSource.h"
#include "media/PdfSource.h"
#include "media/RemoteSource.h"
#include "media/VideoSource.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/ParamWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <string>

namespace bsl::ui {

namespace {

// Informacoes tecnicas do clip (precisam da fonte aberta)
void clipInfo(const Clip& clip)
{
    if (clip.isGenerator()) {
        const GeneratorInfo* info = clip.generatorInfo();
        monoText("Gerador: %s", info ? info->name : "?");
        if (clip.isOpen() && clip.source->width() > 0)
            monoText("%d x %d", clip.source->width(), clip.source->height());
        else if (clip.isLive())
            ImGui::TextColored(theme::kTextDim, "Dispare o clip para iniciar a captura");
        return;
    }
    if (clip.type == Clip::Type::File && media::isRemoteKind(clip.kind)) {
        switch (clip.kind) {
        case media::MediaKind::Document: monoText("Documento PDF"); break;
        case media::MediaKind::Office: monoText("Documento do Office (convertido para PDF)"); break;
        default: monoText("Página web local"); break;
        }
        if (clip.isPaged()) {
            const auto* pdf = dynamic_cast<const media::PdfSource*>(clip.source.get());
            const int total = pdf ? pdf->pageCount() : media::docPageCount(clip.path);
            if (total > 0)
                monoText("Páginas: %d", total);
        }
        if (clip.isOpen() && clip.source->width() > 0)
            monoText("%d x %d", clip.source->width(), clip.source->height());
        else if (!clip.isOpen())
            ImGui::TextColored(theme::kTextDim, "Dispare o clip para abrir");
        return;
    }
    if (!clip.isOpen()) {
        ImGui::TextColored(theme::kTextDim, "Dispare o clip para ver os detalhes");
        return;
    }
    const media::ISource& src = *clip.source;
    if (const auto* video = dynamic_cast<const media::VideoSource*>(&src)) {
        const media::VideoInfo& info = video->info();
        monoText("Vídeo: %s (%s)", info.codec.c_str(), info.decoderName.c_str());
        monoText("Formato: %s", info.pixelFormat.c_str());
        monoText("%d x %d  |  %.2f fps", info.width, info.height, info.fps);
        monoText("Duração: %s", info.duration > 0.0 ? formatTime(info.duration).c_str() : "desconhecida");
        monoText("Alpha: %s", info.hasAlpha ? "sim" : "não");
    } else if (const auto* image = dynamic_cast<const media::ImageSource*>(&src)) {
        monoText(image->isAnimated() ? "Imagem animada (%zu quadros)" : "Imagem estática", image->frameCount());
        monoText("%d x %d", image->width(), image->height());
        if (image->isAnimated())
            monoText("Duração: %s", formatTime(image->duration()).c_str());
        monoText("Alpha: %s", image->hasAlpha() ? "sim" : "não");
    }
}

// Rótulo cinza à esquerda; o controle vem logo depois, alinhado numa coluna fixa
void rowLabel(const char* text)
{
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    dimLabel(text);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + dp(84.0f)); // SameLine(offset) soma o deslocamento da tabela duas vezes
}

void fitCombo(Clip& clip)
{
    rowLabel("Encaixe");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##fit", fitModeName(clip.fit))) {
        for (int i = 0; i < kFitModeCount; ++i) {
            const auto m = static_cast<FitMode>(i);
            if (ImGui::Selectable(fitModeName(m), m == clip.fit))
                clip.fit = m;
        }
        ImGui::EndCombo();
    }
}

void statusLine(const Clip& clip)
{
    if (!clip.isOpen())
        return;
    const media::SourceStatus st = clip.source->status();
    if (st.text.empty())
        return;
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(st.level == media::SourceStatus::Level::Warning ? theme::kWarning : theme::kTextDim, "%s",
                       st.text.c_str());
    ImGui::PopTextWrapPos();
}

// PDF / Office: navegação por página (◀ ▶ e "página X de N"), numa linha só, acima das colunas do painel
void pagedHeader(Clip& clip)
{
    auto* pdf = dynamic_cast<media::PdfSource*>(clip.source.get());
    const media::ParamSet params(clip.paramDescs(), clip.params);
    const int total = pdf ? pdf->pageCount() : media::docPageCount(clip.path);
    const int page = std::max(1, pdf ? pdf->page() : params.i("page"));

    auto go = [&](int n) {
        n = std::max(1, total > 0 ? std::min(n, total) : n);
        clip.setParam("page", n); // grava no clip (e no projeto) e avisa a fonte aberta
        if (pdf)
            pdf->goToPage(n); // vale mesmo se o avanço automático já tinha mudado de página
    };

    ImGui::BeginDisabled(page <= 1);
    if (iconButton(ICON_CHEVRON_LEFT, "##paginaAnterior", "Página anterior"))
        go(page - 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(total > 0 && page >= total);
    if (iconButton(ICON_CHEVRON_RIGHT, "##paginaSeguinte", "Próxima página"))
        go(page + 1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (total > 0)
        monoText("página %d de %d", page, total);
    else
        monoText("página %d de ?", page);
}

// Web: Interagir (clique, rolagem e teclado na Prévia vão para a página) e Recarregar
void webHeader(Clip& clip)
{
    ImGui::Checkbox("Interagir", &clip.interact);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Com Interagir ligado, clique, rolagem e teclado na Prévia são enviados à página");
    ImGui::SameLine();
    ImGui::BeginDisabled(!clip.isOpen());
    if (ImGui::Button("Recarregar"))
        if (auto* web = dynamic_cast<media::RemoteSource*>(clip.source.get()))
            web->requestReload();
    ImGui::EndDisabled();
}

// Coluna de controles de PDF/Office: encaixe, situação e a dica da apresentação
void pagedControls(Clip& clip)
{
    fitCombo(clip);
    statusLine(clip);

    const std::string ext = media::lowerExtension(clip.path);
    if (ext == "pptx" || ext == "ppt" || ext == "odp") {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kTextDim,
                           "Para PowerPoint com animações e vídeos, use Captura de tela da janela da apresentação.");
        ImGui::PopTextWrapPos();
    }
}

void clipControls(Clip& clip)
{
    const bool open = clip.isOpen();
    const double duration = open ? clip.source->duration() : 0.0;

    if (clip.isPaged()) {
        pagedControls(clip);
        return;
    }

    // Fonte ao vivo: sem transporte (sempre tocando), so o encaixe
    if (clip.isLive()) {
        ImGui::TextColored(theme::kTextDim, open ? "Fonte ao vivo" : "Fonte ao vivo (parada)");
        fitCombo(clip);
        statusLine(clip);
        return;
    }

    // --- Transporte ---
    ImGui::BeginDisabled(!open);
    const ImVec2 transportBtn(dp(36.0f), ImGui::GetFrameHeight());
    if (iconButton(clip.playing ? ICON_PAUSE : ICON_PLAY, "##clip_tocar_pausar", clip.playing ? "Pausar" : "Tocar",
                   transportBtn, clip.playing ? IconButton_Active : 0))
        clip.setPlaying(!clip.playing);
    ImGui::SameLine();
    if (iconButton(ICON_SKIP_BACK, "##clip_reiniciar", "Reiniciar do começo", transportBtn))
        clip.restart();
    ImGui::EndDisabled();

    // Posicao: arrastar faz seek
    ImGui::BeginDisabled(!open || duration <= 0.0);
    float pos = open ? static_cast<float>(clip.source->position()) : 0.0f;
    const std::string posLabel = formatTime(pos) + " / " + formatTime(duration);
    ImGui::SetNextItemWidth(-FLT_MIN);
    pushMono();
    if (ImGui::SliderFloat("##posicao", &pos, 0.0f, static_cast<float>(duration > 0.0 ? duration : 1.0),
                           posLabel.c_str(), ImGuiSliderFlags_NoRoundToFormat))
        clip.seek(pos);
    popMono();
    ImGui::EndDisabled();

    // Velocidade 0-4 (reset em 1.0)
    rowLabel("Velocidade");
    float speed = clip.speed;
    const float speedRoom = ImGui::GetContentRegionAvail().x;
    if (hotNumber("##velocidade", speed, 0.0f, 4.0f, 0.01f, "%.2f", "x", 1.0f, 64.0f))
        clip.setSpeed(speed);
    if (speedRoom >= dp(64.0f + 52.0f) + ImGui::GetStyle().ItemSpacing.x) { // coluna estreita: só o botão direito (restaurar)
        ImGui::SameLine();
        if (ImGui::Button("1.0x", ImVec2(dp(52.0f), 0.0f)))
            clip.setSpeed(1.0f);
    }

    // Loop: três botões de ícone (rótulo "Loop" só quando a coluna é larga o bastante)
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const bool withLabel = ImGui::GetContentRegionAvail().x >= dp(84.0f + 3.0f * 36.0f) + 2.0f * spacing;
        if (withLabel)
            rowLabel("Loop");
        const float buttonW = withLabel ? dp(36.0f) : std::max(dp(28.0f), (ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f);
        struct LoopChoice
        {
            media::LoopMode mode;
            const char* icon;
            const char* id;
            const char* tooltip;
        };
        const LoopChoice choices[] = {
            {media::LoopMode::Loop, ICON_REPEAT, "##loop_loop", "Loop: repete sem parar"},
            {media::LoopMode::Once, ICON_ARROW_RIGHT_TO_LINE, "##loop_uma_vez", "Uma vez: toca até o fim e para"},
            {media::LoopMode::PingPong, ICON_ARROW_LEFT_RIGHT, "##loop_pingpong", "Ping-pong: vai e volta"},
        };
        for (size_t i = 0; i < std::size(choices); ++i) {
            const LoopChoice& c = choices[i];
            const bool disabled = c.mode == media::LoopMode::PingPong && !clip.supportsPingPong();
            if (i > 0)
                ImGui::SameLine();
            ImGui::BeginDisabled(disabled);
            if (iconButton(c.icon, c.id, disabled ? "Vídeos só tocam para frente (sem ping-pong)." : c.tooltip,
                           ImVec2(buttonW, ImGui::GetFrameHeight()), c.mode == clip.loop ? IconButton_Active : 0))
                clip.setLoop(c.mode);
            ImGui::EndDisabled();
        }
    }

    fitCombo(clip);
    statusLine(clip);
}

// Parametros genericos do clip (gerador) + editores especiais (alvo da captura, dispositivo)
void clipParams(PanelContext& ctx, Clip& clip)
{
    const std::vector<media::ParamDesc> descs = clip.paramDescs();
    if (descs.empty())
        return;
    const Selection sel = ctx.selection;

    const CustomParamEditor editor = [&](const media::ParamDesc& d, const media::Json& current, media::Json& out) {
        if (d.custom == "screen-target") {
            const std::string label = media::targetLabel(current) + "##alvo";
            if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f)))
                requestCaptureTarget(ctx.composition.currentDeck, sel.layer, sel.column);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Clique para escolher outro monitor ou janela (vale também com o clip ativo)");
            return false;
        }
        if (d.custom == "capture-device") {
            media::DeviceCatalog& cat = media::DeviceCatalog::instance();
            cat.ensureListed();
            const std::vector<std::string> names = cat.deviceNames();
            const std::string cur = current.is_string() ? current.get<std::string>() : std::string();
            std::string preview = cur;
            if (cur.empty()) {
                if (!names.empty())
                    preview = "Primeiro disponível (" + names.front() + ")";
                else
                    preview = cat.listing() ? "Procurando dispositivos…" : "Nenhum dispositivo encontrado";
            }
            const float btn = dp(54.0f);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
            bool changed = false;
            if (beginCombo("##dispositivo", preview.c_str())) {
                if (ImGui::Selectable("Primeiro disponível", cur.empty())) {
                    out = std::string();
                    changed = true;
                }
                bool curListed = cur.empty();
                for (const std::string& n : names) {
                    curListed = curListed || n == cur;
                    if (ImGui::Selectable(n.c_str(), n == cur)) {
                        out = n;
                        changed = true;
                    }
                }
                if (!curListed)
                    ImGui::TextColored(theme::kWarning, "%s (ausente)", cur.c_str());
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Buscar", ImVec2(btn, 0.0f)))
                cat.refresh();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Procurar dispositivos de novo");
            return changed;
        }
        return false;
    };

    for (ParamEdit& e : drawParams(descs, clip.params, editor))
        clip.setParam(e.id, std::move(e.value));
}

// Area tracejada onde os efeitos serao soltos (Fase 4)
void effectsDropArea()
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, dp(40.0f));
    size.y = std::max(size.y, dp(56.0f));
    ImGui::Dummy(size);
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawDashedRect(dl, a, b, ImGui::GetColorU32(theme::kPanelActive));
    const char* text = "Arraste efeitos aqui (Fase 4)";
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), ImGui::GetColorU32(theme::kTextDim), text);
}

} // namespace

void drawClipPanel(PanelContext& ctx)
{
    beginPanel(panel::kClip);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, dp(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, dp(theme::kSpacing, 6.0f));

    Composition& comp = ctx.composition;
    const Selection& sel = ctx.selection;

    if (sel.kind == Selection::Kind::Clip && comp.validCell(sel.layer, sel.column)
        && !comp.clip(sel.layer, sel.column).empty()) {
        Clip& clip = comp.clip(sel.layer, sel.column);

        ImGui::PushFont(fonts().bold, fonts().bold->LegacySize);
        ImGui::TextWrapped("%s", clip.name.c_str());
        ImGui::PopFont();

        if (clip.isPaged())
            pagedHeader(clip);
        else if (clip.isWeb())
            webHeader(clip);

        if (clip.isRemote()) {
            // PDF/Office/Web: coluna única (os controles e a lista de parâmetros precisam da largura toda)
            ImGui::Spacing();
            clipControls(clip);
            ImGui::Spacing();
            if (clip.type == Clip::Type::File) {
                pushMono();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
                ImGui::TextWrapped("%s", paths::toUtf8(clip.path).c_str());
                ImGui::PopStyleColor();
                popMono();
                ImGui::Spacing();
            }
            clipInfo(clip);
            clipParams(ctx, clip);
        } else {
        const bool wide = !clip.paramDescs().empty(); // parametros precisam de mais largura
        if (ImGui::BeginTable("##clipCols", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch, wide ? 1.7f : 1.0f);
            ImGui::TableSetupColumn("transporte", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (clip.type == Clip::Type::File) {
                pushMono();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
                ImGui::TextWrapped("%s", paths::toUtf8(clip.path).c_str());
                ImGui::PopStyleColor();
                popMono();
                ImGui::Spacing();
            }
            clipInfo(clip);
            clipParams(ctx, clip);

            ImGui::TableSetColumnIndex(1);
            clipControls(clip);
            ImGui::EndTable();
        }
        }

        ImGui::Spacing();
        effectsDropArea();
    } else if (sel.kind == Selection::Kind::Clip && comp.validCell(sel.layer, sel.column)) {
        ImGui::TextColored(theme::kTextDim, "Célula vazia (camada %d, coluna %d)", sel.layer + 1, sel.column + 1);
    } else {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kTextDim, "Selecione um clip na grade para ver e ajustar os parâmetros.");
        ImGui::PopTextWrapPos();
    }

    ImGui::PopStyleVar(2);
    endPanel();
}

} // namespace bsl::ui
