#include "ui/panels/ClipPanel.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "app/Qa.h"
#include "media/CaptureTargets.h"
#include "media/ControlPanel.h"
#include "media/DeviceCatalog.h"
#include "media/DocInfo.h"
#include "media/ImageSource.h"
#include "media/PdfSource.h"
#include "media/RemoteSource.h"
#include "media/VideoSource.h"
#include "media/WebSource.h"
#include "ui/Theme.h"
#include "ui/Ui.h"
#include "ui/Widgets.h"
#include "ui/panels/CaptureTargetChooser.h"
#include "ui/panels/DmxWindow.h"
#include "ui/panels/EffectChainWidget.h"
#include "ui/panels/ParamWidgets.h"
#include "ui/panels/TransformEditor.h"
#include "ui/panels/VideoCompat.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <string>

namespace bsl::ui {

namespace {

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

void rowLabel(const char* text)
{
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    dimLabel(text);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + dp(84.0f));
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
    ImGui::TextColored(st.level == media::SourceStatus::Level::Error     ? theme::kError
                       : st.level == media::SourceStatus::Level::Warning ? theme::kWarning
                                                                         : theme::kTextDim,
                       "%s", st.text.c_str());
    ImGui::PopTextWrapPos();
}

void replayCombo(Clip& clip)
{
    if (!clip.supportsReplayMode())
        return;
    rowLabel("Ao disparar");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (beginCombo("##ao_disparar_de_novo", replayModeName(clip.replay))) {
        for (const ReplayMode m : {ReplayMode::Resume, ReplayMode::Restart}) {
            if (ImGui::Selectable(replayModeName(m), m == clip.replay))
                clip.replay = m;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(clip.type == Clip::Type::File && clip.kind == media::MediaKind::Video
                              ? "Ao disparar de novo: Retomar volta para o ponto em que o vídeo parou; Reiniciar começa do início."
                              : "Ao disparar de novo: Retomar mantém a fonte viva em segundo plano enquanto outro clip toca na camada "
                                "(a página não recarrega, a conexão não cai); Reiniciar fecha e abre do início.");
    }
}

void pagedHeader(Clip& clip)
{
    auto* pdf = dynamic_cast<media::PdfSource*>(clip.liveSource());
    const media::ParamSet params(clip.paramDescs(), clip.params);
    const int total = pdf ? pdf->pageCount() : media::docPageCount(clip.path);
    const int page = std::max(1, pdf ? pdf->page() : params.i("page"));

    auto go = [&](int n) {
        n = std::max(1, total > 0 ? std::min(n, total) : n);
        clip.setParam("page", n);
        if (pdf)
            pdf->goToPage(n);
    };

    ImGui::BeginDisabled(!pdf);
    if (iconButton(ICON_SKIP_BACK, "##paginaInicio", "Voltar ao início (slide inicial)") && pdf)
        go(pdf->homePage());
    ImGui::EndDisabled();
    ImGui::SameLine();
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
    ImGui::BeginDisabled(!pdf);
    if (iconButton(ICON_REFRESH, "##documentoRecarregar",
                   "Recarregar o documento (use depois de salvar a apresentação; fica no slide atual)") && pdf)
        pdf->reloadDocument();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (total > 0)
        monoText("página %d de %d", page, total);
    else
        monoText("página %d de ?", page);
}

void webHeader(Clip& clip)
{
    ImGui::Checkbox("Interagir", &clip.interact);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Com Interagir ligado, clique, rolagem e teclado na Prévia são enviados à página");
    ImGui::SameLine();
    ImGui::BeginDisabled(!clip.liveSource());
    if (ImGui::Button("Recarregar"))
        if (auto* web = dynamic_cast<media::RemoteSource*>(clip.liveSource()))
            web->requestReload();
    ImGui::EndDisabled();
}

void controlPanelUi(PanelContext& ctx, Clip& clip)
{
    auto* panel = dynamic_cast<media::IControlPanel*>(clip.liveSource());
    ImGui::Spacing();
    dimLabel("Painel de controle");
    bool window = ctx.settings.controlPanelMode == "window";
    bool changed = false;
    if (ImGui::RadioButton("Navegador##cp_navegador", !window)) {
        window = false;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Janela própria##cp_janela", window)) {
        window = true;
        changed = true;
    }
    if (changed) {
        ctx.settings.controlPanelMode = window ? "window" : "browser";
        ctx.settings.save();
    }
    ImGui::BeginDisabled(!panel);
    if (ImGui::Button("Abrir painel de controle##cp_abrir", ImVec2(-FLT_MIN, 0.0f)) && panel) {
        std::string url, error;
        if (!panel->openControlServer(url, &error)) {
            log::warn("Painel de controle: " + error);
        } else if (qa::enabled()) {
            log::info("Painel de controle (modo de teste, não abre janela nem navegador): " + url);
        } else if (window && media::WebSource::available()) {
            if (!media::openUrlInOwnWindow(url, "BSL WebBackgrounds - " + clip.name, &error)) {
                log::warn("Painel de controle: " + error + "; abrindo no navegador.");
                media::openUrlInBrowser(url, &error);
            }
        } else if (!media::openUrlInBrowser(url, &error)) {
            log::warn("Painel de controle: " + error);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(panel ? "Liga o servidor local (só neste computador) e abre o painel de controle. Ele controla esta "
                                  "mesma instância, sem reiniciar a página."
                                : "Dispare o clip primeiro: o painel controla a instância que está tocando.");
    if (panel && !panel->controlUrl().empty()) {
        pushMono();
        ImGui::TextColored(theme::kTextDim, "%s%s", panel->controlUrl().c_str(),
                           panel->controlExposedToNetwork() ? "  (aberto na rede)" : "  (só neste computador)");
        popMono();
    }
}

void pagedControls(Clip& clip)
{
    fitCombo(clip);
    replayCombo(clip);
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

void clipControls(Clip& clip, int layerIndex)
{
    const bool open = clip.isOpen();
    const double duration = open ? clip.source->duration() : 0.0;

    if (clip.hasPageNav()) {
        pagedControls(clip);
        return;
    }

    if (clip.isLive()) {
        ImGui::TextColored(theme::kTextDim, open ? "Fonte ao vivo"
                                             : clip.isParked() ? "Fonte ao vivo (em segundo plano)"
                                                               : "Fonte ao vivo (parada)");
        fitCombo(clip);
        replayCombo(clip);
        statusLine(clip);
        return;
    }

    ImGui::BeginDisabled(!open);
    const ImVec2 transportBtn(dp(36.0f), ImGui::GetFrameHeight());
    if (iconButton(clip.playing ? ICON_PAUSE : ICON_PLAY, "##clip_tocar_pausar", clip.playing ? "Pausar" : "Tocar",
                   transportBtn, clip.playing ? IconButton_Active : 0))
        clip.setPlaying(!clip.playing);
    ImGui::SameLine();
    if (iconButton(ICON_SKIP_BACK, "##clip_reiniciar", "Reiniciar do começo", transportBtn))
        clip.restart();
    ImGui::EndDisabled();

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

    rowLabel("Velocidade");
    float speed = clip.speed;
    const float speedRoom = ImGui::GetContentRegionAvail().x;
    hotDmx({dmx::ParamKind::LayerSpeed, dmxLayerIdOf(layerIndex)});
    if (hotNumber("##velocidade", speed, 0.0f, 4.0f, 0.01f, "%.2f", "x", 1.0f, 64.0f))
        clip.setSpeed(speed);
    if (speedRoom >= dp(64.0f + 52.0f) + ImGui::GetStyle().ItemSpacing.x) {
        ImGui::SameLine();
        if (ImGui::Button("1.0x", ImVec2(dp(52.0f), 0.0f)))
            clip.setSpeed(1.0f);
    }

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
    replayCombo(clip);
    statusLine(clip);
}

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
        if (d.custom == "ndi-source") {
            std::string chosen;
            if (drawNdiSourcePicker(current.is_string() ? current.get<std::string>() : std::string(), chosen)) {
                out = chosen;
                return true;
            }
            return false;
        }
        if (d.custom == "spout-sender") {
            std::string chosen;
            if (drawSpoutSenderPicker(current.is_string() ? current.get<std::string>() : std::string(), chosen)) {
                out = chosen;
                return true;
            }
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

}

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

        if (clip.missingPlugin) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(theme::kWarning,
                               "Plugin ausente: \"%s\" não existe nesta versão do BSL Stage. A célula não toca, mas o "
                               "projeto guarda o plugin e os parâmetros.",
                               clip.generatorId.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            ImGui::SeparatorText("Efeitos");
            drawEffectChain(clip.effects, "clip", nullptr, kNoZoneSource, EffectOwner{0, comp.currentDeck, sel.layer, sel.column});
            ImGui::PopStyleVar(2);
            endPanel();
            return;
        }

        if (clip.hasPageNav())
            pagedHeader(clip);
        else if (clip.isWeb())
            webHeader(clip);
        if (clip.generatorId == kBslWebBackgroundsId)
            controlPanelUi(ctx, clip);

        if (clip.isRemote()) {
            ImGui::Spacing();
            clipControls(clip, sel.layer);
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
        const bool wide = !clip.paramDescs().empty();
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
            clipControls(clip, sel.layer);
            ImGui::EndTable();
        }
        }

        ImGui::Spacing();
        drawClipTransformToggle(ctx, clip, sel.layer);

        ImGui::Spacing();
        ImGui::SeparatorText("Efeitos");
        drawEffectChain(clip.effects, "clip", nullptr, kNoZoneSource, EffectOwner{0, comp.currentDeck, sel.layer, sel.column});
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

}
