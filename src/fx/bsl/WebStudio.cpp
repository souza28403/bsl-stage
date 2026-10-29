#include "fx/bsl/WebStudio.h"

#include "app/Log.h"
#include "app/Paths.h"
#include "fx/bsl/SliceState.h"
#include "net/MiniHttp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace bsl::fx::bsllight {

using media::Json;
using media::ParamDesc;
using media::ParamSet;
using media::ParamValues;
using namespace media::param;

namespace {

// ----------------------------------------------------------------------------------------------- dados

StudioData loadStudio()
{
    StudioData s;
    const std::filesystem::path file = paths::assetsDir() / L"bsl" / L"web_bg" / L"studio.json";
    std::string text;
    Json j;
    if (net::readWholeFile(file, text, 4u << 20))
        j = Json::parse(text, nullptr, false);
    if (!j.is_object()) {
        log::warn("BSL WebBackgrounds: assets/bsl/web_bg/studio.json não encontrado; usando só o fundo Silk");
        StudioBackground silk;
        silk.id = "Silk";
        silk.label = "Silk";
        silk.category = "fluido";
        s.backgrounds.push_back(silk);
        StudioPalette bsl;
        bsl.id = "bsl";
        bsl.name = "BSL";
        bsl.colors = {"#e8343a", "#7a0d12", "#ffffff", "#1a1a1a", "#0b0b0c"};
        s.palettes.push_back(bsl);
        for (const char* f : {"Bebas Neue", "Anton"}) {
            StudioNamed n;
            n.id = n.label = f;
            n.weights = {400};
            s.fonts.push_back(n);
        }
        StudioNamed fade;
        fade.id = "fade";
        fade.label = "Fade";
        s.textIn = s.textLoop = s.textOut = {fade};
        return s;
    }
    auto str = [](const Json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    for (const Json& b : j.value("backgrounds", Json::array())) {
        StudioBackground bg;
        bg.id = str(b, "id");
        bg.label = str(b, "label");
        bg.category = str(b, "category");
        const Json params = j.value("params", Json::object()).value(bg.id, Json::array());
        for (const Json& p : params) {
            StudioBgParam sp;
            sp.id = str(p, "id");
            sp.label = str(p, "label");
            sp.type = str(p, "type");
            if (sp.type.empty())
                sp.type = "number";
            sp.min = p.value("min", 0.0);
            sp.max = p.value("max", 1.0);
            sp.step = p.value("step", 0.01);
            if (p.contains("default"))
                sp.def = p["default"];
            if (p.contains("options") && p["options"].is_array())
                for (const Json& o : p["options"])
                    sp.options.push_back(o.is_string() ? o.get<std::string>() : o.dump());
            bg.params.push_back(std::move(sp));
        }
        if (!bg.id.empty())
            s.backgrounds.push_back(std::move(bg));
    }
    for (const Json& p : j.value("palettes", Json::array())) {
        StudioPalette pal;
        pal.id = str(p, "id");
        pal.name = str(p, "name");
        for (const Json& c : p.value("colors", Json::array()))
            if (c.is_string())
                pal.colors.push_back(c.get<std::string>());
        while (pal.colors.size() < 5)
            pal.colors.push_back("#000000");
        s.palettes.push_back(std::move(pal));
    }
    auto named = [&](const char* key, std::vector<StudioNamed>& out) {
        for (const Json& n : j.value(key, Json::array())) {
            StudioNamed item;
            item.id = str(n, "id");
            item.label = str(n, "label");
            if (n.contains("weights") && n["weights"].is_array())
                for (const Json& w : n["weights"])
                    if (w.is_number())
                        item.weights.push_back(w.get<int>());
            out.push_back(std::move(item));
        }
    };
    named("fonts", s.fonts);
    named("textIn", s.textIn);
    named("textLoop", s.textLoop);
    named("textOut", s.textOut);
    s.loaded = !s.backgrounds.empty();
    return s;
}

const StudioBackground* findBg(const std::string& id)
{
    for (const StudioBackground& b : studioData().backgrounds)
        if (b.id == id)
            return &b;
    return nullptr;
}

// ----------------------------------------------------------------------------------------------- campos

enum class FK { Num, Bool, Hex, Text, Enum };
enum EnumKey { EK_None, EK_Bg, EK_Palette, EK_Font, EK_Weight, EK_Fill, EK_Position, EK_In, EK_Loop, EK_Out, EK_Mirror, EK_Posterize, EK_Mode };

struct Field
{
    const char* path;
    FK kind;
    double def;
    double lo, hi;
    const char* defText; // Hex / Text / Enum
    EnumKey ek;
    const char* label;
    const char* fmt;
    const char* group;
    const char* tip;
};

const char* const gFundo = "Fundo";
const char* const gCores = "Cores";
const char* const gImagem = "Imagem";
const char* const gMov = "Movimento";
const char* const gEsp = "Espelho e pixel";
const char* const gTex = "Textura";
const char* const gRitmo = "Ritmo e saída";
const char* const gTexto = "Texto e logo";
const char* const gAnim = "Animação do texto";

const std::vector<Field>& fields()
{
    static const std::vector<Field> f = {
        {"bg.id", FK::Enum, 0, 0, 0, "Silk", EK_Bg, "Fundo", "", gFundo, "O fundo ao vivo (41 estilos). Cada um tem os próprios ajustes logo abaixo"},
        {"bg.speed", FK::Num, 0.5, 0.0, 2.0, "", EK_None, "Velocidade", "%.2f", gFundo, "Velocidade da animação do fundo"},
        {"fx.freeze", FK::Bool, 0, 0, 0, "", EK_None, "Congelar fundo", "", gFundo, "Pausa o tempo do fundo"},
        {"bg.transition", FK::Num, 1.2, 0.0, 5.0, "", EK_None, "Transição", "%.1f s", gFundo, "Segundos de fusão ao trocar de fundo (0 = corte seco)"},

        {"palette.id", FK::Enum, 0, 0, 0, "bsl", EK_Palette, "Paleta", "", gCores, "As 3 primeiras cores pintam o fundo; a paleta também pode colorir o texto. \"Personalizada\" usa as 5 cores abaixo"},
        {"palette.hueShift", FK::Num, 0.0, -180.0, 180.0, "", EK_None, "Girar matiz", "%.0f °", gCores, "Gira o matiz de todo o fundo"},
        {"palette.hueCycle", FK::Num, 0.0, 0.0, 60.0, "", EK_None, "Ciclo de cor automático", "%.0f °/s", gCores, "Graus por segundo de rotação automática do matiz (0 = desligado)"},

        {"fx.brightness", FK::Num, 1.0, 0.0, 2.0, "", EK_None, "Brilho", "%.2f", gImagem, ""},
        {"fx.contrast", FK::Num, 1.0, 0.0, 2.0, "", EK_None, "Contraste", "%.2f", gImagem, ""},
        {"fx.saturation", FK::Num, 1.0, 0.0, 2.0, "", EK_None, "Saturação", "%.2f", gImagem, ""},
        {"fx.blur", FK::Num, 0.0, 0.0, 40.0, "", EK_None, "Desfoque", "%.1f px", gImagem, "Em pixels a 1080p; acompanha o tamanho da página"},
        {"fx.grayscale", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Preto e branco", "%.2f", gImagem, ""},
        {"fx.sepia", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Sépia", "%.2f", gImagem, ""},
        {"fx.invert", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Negativo", "%.2f", gImagem, ""},
        {"fx.duotone", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Duotone (cores da paleta)", "%.2f", gImagem, "Mistura um duotone entre as cores 4 e 1 da paleta"},
        {"fx.tint", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Tingir", "%.2f", gImagem, "Lava de cor sobre o fundo, com a cor abaixo"},
        {"fx.tintColor", FK::Hex, 0, 0, 0, "#e8343a", EK_None, "Cor do tingimento", "", gImagem, ""},
        {"fx.posterize", FK::Enum, 0, 0, 0, "0", EK_Posterize, "Posterizar", "", gImagem, "Número de níveis de cor por canal (0 = desligado)"},

        {"fx.zoom", FK::Num, 1.0, 1.0, 3.0, "", EK_None, "Zoom", "%.2fx", gMov, ""},
        {"fx.rotate", FK::Num, 0.0, -180.0, 180.0, "", EK_None, "Rotação", "%.0f °", gMov, ""},
        {"fx.spin", FK::Num, 0.0, -60.0, 60.0, "", EK_None, "Giro contínuo", "%.0f °/s", gMov, ""},
        {"fx.drift", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Deriva lenta (Ken Burns)", "%.2f", gMov, ""},
        {"fx.shake", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Tremor na batida", "%.2f", gMov, "Usa o BPM abaixo"},

        {"fx.mirror", FK::Enum, 0, 0, 0, "none", EK_Mirror, "Espelho", "", gEsp, ""},
        {"fx.pixelate", FK::Num, 1.0, 1.0, 40.0, "", EK_None, "Pixelar", "%.0fx", gEsp, "Pixelar também deixa o fundo mais leve para o computador"},

        {"fx.ledGrid", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Grade de LED", "%.2f", gTex, ""},
        {"fx.ledSize", FK::Num, 8.0, 4.0, 32.0, "", EK_None, "Tamanho do LED", "%.0f px", gTex, ""},
        {"fx.scanlines", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Linhas de TV (CRT)", "%.2f", gTex, ""},
        {"fx.grain", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Granulado", "%.2f", gTex, ""},
        {"fx.vignette", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Vinheta", "%.2f", gTex, ""},
        {"fx.lightLeaks", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Vazamento de luz", "%.2f", gTex, ""},
        {"fx.letterbox", FK::Num, 0.0, 0.0, 0.35, "", EK_None, "Faixas de cinema", "%.2f", gTex, ""},

        {"fx.bpm", FK::Num, 120.0, 40.0, 220.0, "", EK_None, "BPM", "%.0f", gRitmo, "Ritmo usado pelo pulso, strobo e tremor na batida"},
        {"fx.beatPulse", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Pulso de brilho", "%.2f", gRitmo, ""},
        {"fx.strobe", FK::Num, 0.0, 0.0, 1.0, "", EK_None, "Strobo na batida", "%.2f", gRitmo, ""},
        {"fx.blackout", FK::Bool, 0, 0, 0, "", EK_None, "Blackout", "", gRitmo, "Apaga toda a saída (fade para preto)"},
        {"fx.blackoutTime", FK::Num, 0.6, 0.0, 4.0, "", EK_None, "Tempo do blackout", "%.1f s", gRitmo, ""},

        {"text.visible", FK::Bool, 0, 0, 0, "", EK_None, "No ar", "", gTexto, "Coloca o texto ou logo no ar (entrada e loop) ou o tira (saída)"},
        {"text.mode", FK::Enum, 0, 0, 0, "text", EK_Mode, "Tipo", "", gTexto, "Texto ou logo (imagem PNG)"},
        {"text.content", FK::Text, 0, 0, 0, "BSL LIGHT", EK_None, "Texto", "", gTexto, "O texto do telão"},
        {"text.font", FK::Enum, 0, 0, 0, "Bebas Neue", EK_Font, "Fonte", "", gTexto, ""},
        {"text.weight", FK::Enum, 0, 0, 0, "400", EK_Weight, "Peso", "", gTexto, "Só as fontes com mais de um peso respondem"},
        {"text.size", FK::Num, 12.0, 2.0, 60.0, "", EK_None, "Tamanho", "%.1f %%", gTexto, "Largura do bloco (logo) ou tamanho da fonte (texto), em % da largura"},
        {"text.tracking", FK::Num, 0.02, -0.1, 0.5, "", EK_None, "Espaço entre letras", "%.3f em", gTexto, ""},
        {"text.lineHeight", FK::Num, 1.0, 0.7, 1.6, "", EK_None, "Altura da linha", "%.2f", gTexto, ""},
        {"text.uppercase", FK::Bool, 1, 0, 0, "", EK_None, "Tudo maiúsculo", "", gTexto, ""},
        {"text.fill", FK::Enum, 0, 0, 0, "solid", EK_Fill, "Preenchimento", "", gTexto, "Cor sólida ou degradê com as cores da paleta"},
        {"text.color", FK::Hex, 0, 0, 0, "#ffffff", EK_None, "Cor do texto", "", gTexto, ""},
        {"text.stroke", FK::Num, 0.0, 0.0, 12.0, "", EK_None, "Contorno", "%.1f px", gTexto, ""},
        {"text.strokeColor", FK::Hex, 0, 0, 0, "#000000", EK_None, "Cor do contorno", "", gTexto, ""},
        {"text.glow", FK::Num, 0.3, 0.0, 1.0, "", EK_None, "Brilho (glow)", "%.2f", gTexto, ""},
        {"text.shadow", FK::Num, 0.4, 0.0, 1.0, "", EK_None, "Sombra", "%.2f", gTexto, ""},
        {"text.position", FK::Enum, 0, 0, 0, "mc", EK_Position, "Posição", "", gTexto, "Âncora do bloco na tela (9 pontos)"},
        {"text.offsetX", FK::Num, 0.0, -50.0, 50.0, "", EK_None, "Deslocar horizontal", "%.1f %%", gTexto, ""},
        {"text.offsetY", FK::Num, 0.0, -50.0, 50.0, "", EK_None, "Deslocar vertical", "%.1f %%", gTexto, ""},

        {"text.in", FK::Enum, 0, 0, 0, "fade-up", EK_In, "Entrada", "", gAnim, ""},
        {"text.inDuration", FK::Num, 1.0, 0.2, 4.0, "", EK_None, "Duração da entrada", "%.1f s", gAnim, ""},
        {"text.loop", FK::Enum, 0, 0, 0, "float", EK_Loop, "Loop", "", gAnim, ""},
        {"text.loopSpeed", FK::Num, 1.0, 0.2, 3.0, "", EK_None, "Velocidade do loop", "%.1fx", gAnim, ""},
        {"text.out", FK::Enum, 0, 0, 0, "fade", EK_Out, "Saída", "", gAnim, ""},
        {"text.outDuration", FK::Num, 0.8, 0.2, 4.0, "", EK_None, "Duração da saída", "%.1f s", gAnim, ""},
        {"text.stagger", FK::Num, 0.04, 0.0, 0.2, "", EK_None, "Atraso entre letras", "%.3f s", gAnim, "Para as animações letra por letra"},
        {"text.auto.enabled", FK::Bool, 0, 0, 0, "", EK_None, "Ciclo automático", "", gAnim, "Entra e sai sozinho"},
        {"text.auto.hold", FK::Num, 6.0, 1.0, 60.0, "", EK_None, "Tempo na tela", "%.1f s", gAnim, ""},
        {"text.auto.off", FK::Num, 4.0, 0.0, 60.0, "", EK_None, "Tempo fora", "%.1f s", gAnim, ""},
    };
    return f;
}

std::string fieldId(const char* path)
{
    std::string id = path;
    std::replace(id.begin(), id.end(), '.', '_');
    return id;
}

std::string bgParamId(const std::string& bg, const std::string& param)
{
    return "bgp_" + bg + "_" + param;
}

struct EnumList
{
    std::vector<media::ParamOption> options;
    bool numeric = false;
};

EnumList enumOptions(EnumKey key)
{
    EnumList out;
    const StudioData& d = studioData();
    auto fromNamed = [&](const std::vector<StudioNamed>& list) {
        for (const StudioNamed& n : list)
            out.options.push_back({n.id, n.label});
    };
    switch (key) {
    case EK_Bg:
        for (const StudioBackground& b : d.backgrounds)
            out.options.push_back({b.id, b.label});
        out.options.push_back({"none", "Nenhum (só texto e logo)"}); // a pagina aceita bg "none": combine com Fundo transparente
        break;
    case EK_Palette:
        for (const StudioPalette& p : d.palettes)
            out.options.push_back({p.id, p.name});
        out.options.push_back({"custom", "Personalizada"});
        break;
    case EK_Font: fromNamed(d.fonts); break;
    case EK_Weight:
        out.options = {{"400", "400 (normal)"}, {"700", "700 (negrito)"}, {"900", "900 (preto)"}};
        out.numeric = true;
        break;
    case EK_Fill: out.options = {{"solid", "Cor"}, {"palette", "Paleta"}}; break;
    case EK_Position:
        out.options = {{"tl", "Topo à esquerda"}, {"tc", "Topo ao centro"}, {"tr", "Topo à direita"},
                       {"ml", "Meio à esquerda"}, {"mc", "Centro"},         {"mr", "Meio à direita"},
                       {"bl", "Base à esquerda"}, {"bc", "Base ao centro"}, {"br", "Base à direita"}};
        break;
    case EK_In: fromNamed(d.textIn); break;
    case EK_Loop: fromNamed(d.textLoop); break;
    case EK_Out: fromNamed(d.textOut); break;
    case EK_Mirror:
        out.options = {{"none", "Nenhum"}, {"h", "Lateral"}, {"v", "Vertical"}, {"quad", "4 lados"}};
        break;
    case EK_Posterize:
        out.options = {{"0", "Desligado"}, {"3", "3"}, {"4", "4"}, {"6", "6"}, {"8", "8"}};
        out.numeric = true;
        break;
    case EK_Mode: out.options = {{"text", "Texto"}, {"logo", "Logo (PNG)"}}; break;
    case EK_None: break;
    }
    return out;
}

media::Color colorFromHex(const std::string& hex, media::Color fallback = {})
{
    float rgb[3];
    if (!parseHexColor(hex, rgb))
        return fallback;
    return {rgb[0], rgb[1], rgb[2], 1.0f};
}

std::string hexFromColor(const media::Color& c)
{
    const float rgb[3] = {c.r, c.g, c.b};
    return formatHexColor(rgb);
}

void setPath(Json& root, const std::string& path, Json value)
{
    Json* cur = &root;
    size_t pos = 0;
    while (true) {
        const size_t dot = path.find('.', pos);
        if (dot == std::string::npos) {
            (*cur)[path.substr(pos)] = std::move(value);
            return;
        }
        cur = &(*cur)[path.substr(pos, dot - pos)];
        if (!cur->is_object())
            *cur = Json::object();
        pos = dot + 1;
    }
}

const Json* getPath(const Json& root, const std::string& path)
{
    const Json* cur = &root;
    size_t pos = 0;
    while (true) {
        if (!cur->is_object())
            return nullptr;
        const size_t dot = path.find('.', pos);
        const std::string key = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        const auto it = cur->find(key);
        if (it == cur->end())
            return nullptr;
        cur = &*it;
        if (dot == std::string::npos)
            return cur;
        pos = dot + 1;
    }
}

int decimalsOf(double step)
{
    if (step >= 1.0)
        return 0;
    int n = 0;
    double v = step;
    while (n < 4 && std::fabs(v - std::round(v)) > 1e-9) {
        v *= 10.0;
        ++n;
    }
    return n;
}

bool sameNumber(const Json& a, const Json& b)
{
    if (a.is_number() && b.is_number())
        return std::fabs(a.get<double>() - b.get<double>()) < 1e-6;
    return a == b;
}

} // namespace

const StudioData& studioData()
{
    static const StudioData data = loadStudio();
    return data;
}

// ----------------------------------------------------------------------------------------------- descritores

std::vector<ParamDesc> describeStudioParams(const ParamValues& values)
{
    std::vector<ParamDesc> d;
    const StudioData& studio = studioData();
    std::string bgId = "Silk";
    if (const Json* v = values.find("bg_id"); v && v->is_string())
        bgId = v->get<std::string>();
    const StudioBackground* bg = findBg(bgId);
    if (!bg && !studio.backgrounds.empty())
        bg = &studio.backgrounds.front();

    auto emit = [&](const Field& f) {
        const std::string id = fieldId(f.path);
        ParamDesc pd;
        switch (f.kind) {
        case FK::Num: pd = makeFloat(id, f.label, static_cast<float>(f.def), static_cast<float>(f.lo), static_cast<float>(f.hi), f.fmt, f.group); break;
        case FK::Bool: pd = makeBool(id, f.label, f.def != 0.0, f.group); break;
        case FK::Hex: pd = makeColor(id, f.label, colorFromHex(f.defText), f.group); break;
        case FK::Text: pd = makeText(id, f.label, f.defText, false, f.group); break;
        case FK::Enum: {
            EnumList list = enumOptions(f.ek);
            pd = makeEnum(id, f.label, list.options, f.defText, f.group);
            break;
        }
        }
        pd.tooltip = f.tip;
        d.push_back(std::move(pd));
    };

    for (const Field& f : fields()) {
        emit(f);
        const std::string path = f.path;
        if (path == "bg.transition" && bg) {
            // Ajustes do fundo escolhido (os do painel original, com a mesma faixa e padrao)
            const std::string group = "Ajustes · " + bg->label;
            for (const StudioBgParam& p : bg->params) {
                const std::string id = bgParamId(bg->id, p.id);
                ParamDesc pd;
                if (p.type == "color") {
                    pd = makeColor(id, p.label, colorFromHex(p.def.is_string() ? p.def.get<std::string>() : "#ffffff"), group);
                } else if (p.type == "select") {
                    std::vector<media::ParamOption> opts;
                    for (size_t i = 0; i < p.options.size(); ++i)
                        opts.push_back({std::to_string(i), p.options[i]});
                    pd = makeEnum(id, p.label, opts, std::to_string(p.def.is_number() ? p.def.get<int>() : 0), group);
                } else if (p.min == 0.0 && p.max == 1.0 && p.step == 1.0) {
                    pd = makeBool(id, p.label, p.def.is_number() && p.def.get<double>() != 0.0, group);
                } else {
                    char fmt[16];
                    std::snprintf(fmt, sizeof(fmt), "%%.%df", decimalsOf(p.step));
                    pd = makeFloat(id, p.label, p.def.is_number() ? p.def.get<float>() : 0.0f, static_cast<float>(p.min),
                                   static_cast<float>(p.max), fmt, group);
                }
                d.push_back(std::move(pd));
            }
        }
        if (path == "palette.hueCycle") {
            // As 5 cores da paleta personalizada
            const StudioPalette* base = studio.palettes.empty() ? nullptr : &studio.palettes.front();
            for (int i = 0; i < 5; ++i) {
                const std::string hex = base && i < static_cast<int>(base->colors.size()) ? base->colors[static_cast<size_t>(i)] : "#ffffff";
                ParamDesc pd = makeColor("palette_c" + std::to_string(i + 1), "Cor " + std::to_string(i + 1), colorFromHex(hex), gCores);
                pd.enabledIf = "palette_id=custom";
                pd.tooltip = i < 3 ? "Uma das 3 primeiras cores, que pintam o fundo" : "Cor extra da paleta personalizada";
                d.push_back(std::move(pd));
            }
        }
        if (path == "fx.strobe") {
            ParamDesc flash = makeCustom("fx_flash", "Flash", "event", Json(0), gRitmo);
            flash.tooltip = "Dispara um flash branco por cima de tudo";
            d.push_back(std::move(flash));
        }
        if (path == "text.mode") {
            ParamDesc logo = makeFile("logoFile", "Logo (imagem)", "Imagens (*.png;*.webp;*.jpg;*.svg)|*.png;*.webp;*.jpg;*.jpeg;*.svg", gTexto);
            logo.tooltip = "Imagem do logo (PNG com transparência é o ideal). Ao escolher, o tipo muda para Logo";
            d.push_back(std::move(logo));
        }
    }
    return d;
}

// ----------------------------------------------------------------------------------------------- estado

Json buildStudioState(const ParamSet& p)
{
    const StudioData& studio = studioData();
    Json state = Json::object();
    state["v"] = 1;
    for (const Field& f : fields()) {
        const std::string id = fieldId(f.path);
        Json v;
        switch (f.kind) {
        case FK::Num: v = floatJson(p.f(id)); break;
        case FK::Bool: v = p.b(id); break;
        case FK::Hex: v = hexFromColor(p.c(id)); break;
        case FK::Text: v = p.s(id); break;
        case FK::Enum: {
            const std::string s = p.s(id);
            const EnumList list = enumOptions(f.ek);
            v = list.numeric ? Json(std::atoi(s.c_str())) : Json(s);
            break;
        }
        }
        setPath(state, f.path, std::move(v));
    }
    // fx.flash: contador de disparos
    {
        const Json raw = p.raw("fx_flash");
        setPath(state, "fx.flash", raw.is_number() ? raw.get<int>() : 0);
    }
    // Paleta: as cores da paleta escolhida, ou as 5 personalizadas
    {
        Json colors = Json::array();
        const std::string palId = p.s("palette_id");
        const StudioPalette* pal = nullptr;
        for (const StudioPalette& cand : studio.palettes)
            if (cand.id == palId)
                pal = &cand;
        for (int i = 0; i < 5; ++i) {
            if (pal)
                colors.push_back(pal->colors[static_cast<size_t>(i)]);
            else
                colors.push_back(hexFromColor(p.c("palette_c" + std::to_string(i + 1))));
        }
        setPath(state, "palette.colors", std::move(colors));
    }
    // Ajustes do fundo escolhido
    {
        const std::string bgId = p.s("bg_id");
        const StudioBackground* bg = findBg(bgId);
        if (bg) {
            Json params = Json::object();
            for (const StudioBgParam& sp : bg->params) {
                const std::string id = bgParamId(bg->id, sp.id);
                const ParamDesc* d = p.desc(id);
                if (!d)
                    continue;
                if (sp.type == "color")
                    params[sp.id] = hexFromColor(p.c(id));
                else if (sp.type == "select")
                    params[sp.id] = std::atoi(p.s(id).c_str());
                else if (d->type == media::ParamType::Bool)
                    params[sp.id] = p.b(id) ? 1 : 0;
                else
                    params[sp.id] = floatJson(p.f(id));
            }
            setPath(state, "bg.params", Json{{bg->id, std::move(params)}});
        }
    }
    return state;
}

bool applyStudioState(const Json& state, ParamValues& values)
{
    if (!state.is_object())
        return false;
    const StudioData& studio = studioData();
    const ParamSet cur(describeStudioParams(values), values);
    bool changed = false;
    auto put = [&](const std::string& id, Json v) {
        if (v.is_null())
            return;
        if (sameNumber(cur.raw(id), v))
            return;
        values.set(id, std::move(v));
        changed = true;
    };

    for (const Field& f : fields()) {
        const Json* v = getPath(state, f.path);
        if (!v || v->is_null())
            continue;
        const std::string id = fieldId(f.path);
        switch (f.kind) {
        case FK::Num:
            if (v->is_number())
                put(id, floatJson(static_cast<float>(std::clamp(v->get<double>(), f.lo, f.hi))));
            break;
        case FK::Bool:
            if (v->is_boolean())
                put(id, *v);
            else if (v->is_number())
                put(id, v->get<double>() != 0.0);
            break;
        case FK::Hex:
            if (v->is_string()) {
                float rgb[3];
                if (parseHexColor(v->get<std::string>(), rgb))
                    put(id, colorJson({rgb[0], rgb[1], rgb[2], 1.0f}));
            }
            break;
        case FK::Text:
            if (v->is_string())
                put(id, *v);
            break;
        case FK::Enum: {
            std::string s = v->is_string() ? v->get<std::string>() : (v->is_number() ? std::to_string(v->get<int>()) : std::string());
            const EnumList list = enumOptions(f.ek);
            for (const media::ParamOption& o : list.options)
                if (o.value == s) {
                    put(id, Json(s));
                    break;
                }
            break;
        }
        }
    }
    if (const Json* flash = getPath(state, "fx.flash"); flash && flash->is_number()) {
        // O painel soma 1 por flash; o contador do clip acompanha (nunca volta atras)
        const Json mine = cur.raw("fx_flash");
        const int have = mine.is_number() ? mine.get<int>() : 0;
        if (flash->get<int>() > have)
            put("fx_flash", Json(flash->get<int>()));
    }
    // Cores da paleta: personalizada guarda as 5; uma paleta conhecida ja define as cores
    if (const Json* cols = getPath(state, "palette.colors"); cols && cols->is_array()) {
        const Json* pid = getPath(state, "palette.id");
        const bool custom = !pid || !pid->is_string() || pid->get<std::string>() == "custom";
        if (custom)
            for (size_t i = 0; i < 5 && i < cols->size(); ++i) {
                float rgb[3];
                if ((*cols)[i].is_string() && parseHexColor((*cols)[i].get<std::string>(), rgb))
                    put("palette_c" + std::to_string(i + 1), colorJson({rgb[0], rgb[1], rgb[2], 1.0f}));
            }
    }
    // Ajustes por fundo
    if (const Json* bgp = getPath(state, "bg.params"); bgp && bgp->is_object()) {
        for (auto it = bgp->begin(); it != bgp->end(); ++it) {
            const StudioBackground* bg = findBg(it.key());
            if (!bg || !it.value().is_object())
                continue;
            for (const StudioBgParam& sp : bg->params) {
                const auto pv = it.value().find(sp.id);
                if (pv == it.value().end() || pv->is_null())
                    continue;
                const std::string id = bgParamId(bg->id, sp.id);
                if (sp.type == "color") {
                    float rgb[3];
                    if (pv->is_string() && parseHexColor(pv->get<std::string>(), rgb))
                        put(id, colorJson({rgb[0], rgb[1], rgb[2], 1.0f}));
                } else if (sp.type == "select") {
                    if (pv->is_number())
                        put(id, Json(std::to_string(pv->get<int>())));
                } else if (sp.min == 0.0 && sp.max == 1.0 && sp.step == 1.0) {
                    if (pv->is_number())
                        put(id, pv->get<double>() != 0.0);
                } else if (pv->is_number()) {
                    put(id, floatJson(static_cast<float>(std::clamp(pv->get<double>(), sp.min, sp.max))));
                }
            }
        }
    }
    (void)studio;
    return changed;
}

} // namespace bsl::fx::bsllight
