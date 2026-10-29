// Provedor "bsl:" do registro de efeitos (os plugins BSL Light nativos, sempre instalados) e o registro das fontes BSL
// em core/Generators. As descricoes (tooltips) vem dos MANUAL_DE_USO.md dos plugins originais, quando existem.

#include "fx/EffectRegistry.h"
#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/BslSources.h"
#include "ui/Icons.h"

#include <map>
#include <mutex>

namespace bsl::fx {

namespace bsllight {

const std::vector<BslPluginInfo>& bslPlugins()
{
    static const std::vector<BslPluginInfo> kPlugins = {
        {kAlphaReactorId, "BSL Alpha Reactor", "Reativo ao áudio",
         "Aura de raios de luz (god rays) e rastros infinitos atrás do DJ recortado (canal alpha), reagindo ao áudio: os "
         "graves pulsam os rastros e os agudos pulsam a aura. Use logo depois de uma Remoção de Fundo ou Chroma Key na "
         "câmera do DJ.",
         false},
        {kDatamoshId, "BSL Datamosh & Pixel Sort", "Reativo ao áudio",
         "Datamosh em tempo real (derretimento por erro de I-frame) com pixel sorting na GPU, contorno neon (Sobel), "
         "rastro fantasma e máscara de proteção do rosto. O grave reduz o limiar do sort e amplifica o mosh; os agudos "
         "alongam o escorrido.",
         false},
        {kTypoId, "BSL AudioReactive Typo", "Reativo ao áudio",
         "Tipografia cinética e HUD de transmissão (estilo Boiler Room) sobre a câmera do DJ: 4 modos visuais, 4 "
         "mesclagens, REC, timecode, VU meters, tremor no kick, RGB split, P&B rave, scanlines e vinheta. Tudo reage ao "
         "áudio.",
         false},
        {kSliceRefinerId, "BSL Slice Refiner 2", "Slices",
         "Desenha molduras nos slices da saída (11 modos: neon, gradiente, radar, perseguidor, pulsante, máscara de "
         "vídeo, brilhos, glow, onda RGB, pontilhado e glitch), até 8 slices. Usa os slices do próprio BSL Stage; também "
         "pode importar um XML do Resolume.",
         false},
        {kSliceRefiner3Id, "BSL Slice Refiner", "Slices",
         "Recorta o vídeo do clip nos formatos dos slices da saída, em até 8 grupos: cantos arredondados, slices unidos "
         "sem linha no meio, borda com 12 efeitos (manchas, espiral, ondas, zebra, disco, corrida, tracejado, arco-íris, "
         "brilhos, glitch, pulsar, scanner), piscar, bounce no BPM e giro de cor. Usa os slices do próprio BSL Stage; "
         "também pode importar o AdvancedOutput.xml do Resolume. O painel de controle é nativo; com Controle remoto pela "
         "rede dá para usar o painel web de tablet ou celular.",
         false},
        {kVhsId, "BSL VHS", "Estilo",
         "Fita VHS clássica, sem IA: tremor horizontal por linha, faixa de tracking que rola, ruído e chuvisco, cor vazando "
         "(o croma perde resolução e escorre), saturação e contraste de fita, vinheta, linhas de scan e dropouts. Texto "
         "\"PLAY\" e data/hora estilo câmera opcionais. 4 presets (fita gasta, câmera 90s, glitch forte, sutil) e o "
         "tracking pode piscar no BPM.",
         false},
        {kRelightId, "BSL Relight 2.5D", "Iluminação",
         "IA local: estima a profundidade da imagem e a usa como relevo para acender até 4 luzes virtuais 3D sobre o "
         "vídeo (posição X/Y/Z, cor, intensidade, raio, especular e brilho). Tudo é processado neste PC, nada é enviado. "
         "Desligado no perfil Eco.",
         false, true},
        {kBodyGlitchId, "BSL Cybernetic Body-Parts Glitch", "Glitch",
         "IA local: isola as partes do corpo mais próximas da câmera (mãos estendidas, rosto) e só nelas aplica datamosh, "
         "clones atrasados, wireframe 3D ou slicer Voronoi. Tudo é processado neste PC, nada é enviado. Desligado no perfil "
         "Eco.",
         false, true},
        {kAuraId, "BSL Performer Aura & Fluid", "Reativo ao áudio",
         "IA local: usa a máscara da pessoa (DJ, cantor) e o movimento das bordas da silhueta para empurrar um fluido 2D "
         "ao redor do corpo, no telão de LED: aura/fumaça com gradiente de 2 cores, partículas pontilhadas ou grade de "
         "lasers deformada pelo fluxo. Cores, intensidade, dissipação, força do movimento e espessura da aura; a pessoa "
         "pode ficar na frente da aura e os graves reforçam a força. O fluido roda em baixa resolução. Tudo é processado "
         "neste PC, nada é enviado. Desligado no perfil Eco.",
         false, true},
        {kZSliceId, "BSL Z-Slice LED Mapper", "Mapeamento",
         "IA local: estima a profundidade da imagem e a fatia em 3 ou 4 zonas (Frente, Meio, Fundo, Zona 4), com "
         "limiares ajustáveis e suavização entre as zonas. Use na camada ou no master e escolha, em cada slice do "
         "Mapeamento, qual zona ele mostra: a frente nos totens de LED, o fundo na parede principal. O botão "
         "\"Rotear zonas para slices…\" faz isso de uma vez. Tudo é processado neste PC. Desligado no perfil Eco.",
         false, true},
        {kGenBackgroundsId, "BSL Background", "Fundos",
         "Fundos generativos em tempo real, 22 estilos (ondas, células, estrelas, nuvens, plasma, matrix, espiral, cyber "
         "grid, aurora, Balatro, feixes, CRT, dither, galáxia, radar, seda...) com 3 cores, detalhe, densidade, "
         "suavidade, distorção e rotação. Vinheta e grão de filme no final.",
         true},
        {kWebBackgroundsId, "BSL WebBackgrounds", "Fundos",
         "Mais de 40 fundos generativos ao vivo (Silk, Aurora, Galaxy, Balatro...), paletas, efeitos de imagem (espelho, "
         "pixel, LED, CRT, strobo no BPM), texto e logo animados com entrada, loop e saída. Roda a página do BSL Studio "
         "na fonte Web do app; todos os controles são parâmetros do clip.",
         true},
        {kWebMotionId, "BSL WebMotion", "Texto e web",
         "Texto e logo com animação letra por letra (Slide UP ou Zoom IN) e imagens PNG separadas em letras pela "
         "transparência. Com um endereço próprio vira um navegador para vídeos e sites.",
         true},
        {kPptManagerId, "BSL PPT Manager", "Documentos",
         "Apresenta um PPTX, PPT, ODP ou PDF slide a slide: slide anterior e próximo, índice, avanço automático por tempo "
         "ou batidas e transição em fade. A conversão roda em segundo plano (PowerPoint ou LibreOffice), sem travar a saída.",
         true},
        {kLedMapperId, "LED Mapper Pro", "Mapeamento",
         "Grade de cabinets de LED (colunas × linhas, tamanho e espaço) para mapear e calibrar o painel, em dois tons "
         "alternados com borda branca. O botão Exportar… grava um XML no formato Advanced Output do Resolume com um slice "
         "por cabinet.",
         true},
        {kPixelMapPatternId, "Padrão de calibração BSL", "Mapeamento",
         "Desenha, ao vivo no canvas, o padrão de calibração HD de cada painel do modo Pixel Map no seu retângulo escalado "
         "(logo, nome, resolução, coordenada, barras RGBWK, xadrez e círculos). Com o mapeamento criado, o padrão certo "
         "aparece no painel físico real. Opcional: barra que corre e contador de quadros para ver frame drop e travamento.",
         true},
    };
    return kPlugins;
}

const BslPluginInfo* findBslPlugin(const std::string& id)
{
    for (const BslPluginInfo& p : bslPlugins())
        if (id == p.id)
            return &p;
    return nullptr;
}

namespace {

struct Node
{
    GeneratorInfo info;
};

} // namespace

const GeneratorInfo* bslGeneratorInfo(const std::string& id)
{
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Node>> nodes;
    std::lock_guard lock(mutex);
    if (const auto it = nodes.find(id); it != nodes.end())
        return &it->second->info;

    const BslPluginInfo* plugin = findBslPlugin(id);
    if (!plugin || !plugin->generator)
        return nullptr;
    auto node = std::make_unique<Node>();
    node->info.id = plugin->id;
    node->info.name = plugin->name;
    node->info.live = false;
    node->info.needsSetup = false;
    if (id == kGenBackgroundsId) {
        node->info.describe = [](const media::ParamValues&) { return describeGenBackgrounds(); };
        node->info.create = [](int w, int h) { return makeGenBackgrounds(w, h); };
    } else if (id == kWebBackgroundsId) {
        node->info.live = true;
        node->info.describe = [](const media::ParamValues& v) { return describeWebBackgrounds(v); };
        node->info.create = [](int w, int h) { return makeWebBackgrounds(w, h); };
    } else if (id == kWebMotionId) {
        node->info.live = true;
        node->info.describe = [](const media::ParamValues& v) { return describeWebMotion(v); };
        node->info.create = [](int w, int h) { return makeWebMotion(w, h); };
    } else if (id == kPptManagerId) {
        node->info.describe = [](const media::ParamValues& v) { return describePptManager(v); };
        node->info.create = [](int w, int h) { return makePptManager(w, h); };
    } else if (id == kPixelMapPatternId) {
        node->info.describe = [](const media::ParamValues&) { return describePixelMapPattern(); };
        node->info.create = [](int w, int h) { return makePixelMapPattern(w, h); };
    } else {
        node->info.describe = [](const media::ParamValues&) { return describeLedMapper(); };
        node->info.create = [](int w, int h) { return makeLedMapper(w, h); };
    }
    const char* name = plugin->name;
    node->info.label = [name](const media::ParamValues&) { return std::string(name); };
    const GeneratorInfo* result = &node->info;
    nodes.emplace(id, std::move(node));
    return result;
}

} // namespace bsllight

namespace {

using bsllight::BslPluginInfo;

class BslProvider final : public IEffectProvider
{
public:
    std::string prefix() const override { return "bsl:"; }

    static EffectTypeInfo toInfo(const BslPluginInfo& p)
    {
        EffectTypeInfo t;
        t.id = p.id;
        t.name = p.name;
        t.category = p.category;
        t.icon = ICON_ZAP;
        t.section = EffectSection::Bsl;
        t.description = p.description;
        t.generator = p.generator;
        t.ai = p.ai;
        return t;
    }

    std::vector<EffectTypeInfo> list() const override
    {
        std::vector<EffectTypeInfo> out;
        for (const BslPluginInfo& p : bsllight::bslPlugins())
            if (!p.generator)
                out.push_back(toInfo(p));
        return out;
    }

    std::optional<EffectTypeInfo> info(const std::string& typeId) const override
    {
        const BslPluginInfo* p = bsllight::findBslPlugin(typeId);
        if (!p || p->generator)
            return std::nullopt;
        return toInfo(*p);
    }

    std::unique_ptr<IEffect> create(const std::string& typeId) const override
    {
        if (typeId == bsllight::kAlphaReactorId)
            return bsllight::makeAlphaReactor();
        if (typeId == bsllight::kDatamoshId)
            return bsllight::makeDatamosh();
        if (typeId == bsllight::kTypoId)
            return bsllight::makeAudioTypo();
        if (typeId == bsllight::kSliceRefinerId)
            return bsllight::makeSliceRefiner();
        if (typeId == bsllight::kSliceRefiner3Id)
            return bsllight::makeSliceRefiner3();
        if (typeId == bsllight::kVhsId)
            return bsllight::makeVhs();
        if (typeId == bsllight::kRelightId)
            return bsllight::makeRelight();
        if (typeId == bsllight::kBodyGlitchId)
            return bsllight::makeBodyGlitch();
        if (typeId == bsllight::kAuraId)
            return bsllight::makeAura();
        if (typeId == bsllight::kZSliceId)
            return bsllight::makeZSlice();
        return nullptr;
    }

    std::vector<media::ParamDesc> describe(const std::string& typeId) const override
    {
        if (typeId == bsllight::kAlphaReactorId)
            return bsllight::describeAlphaReactor();
        if (typeId == bsllight::kDatamoshId)
            return bsllight::describeDatamosh();
        if (typeId == bsllight::kTypoId)
            return bsllight::describeAudioTypo();
        if (typeId == bsllight::kSliceRefinerId)
            return bsllight::describeSliceRefiner();
        if (typeId == bsllight::kSliceRefiner3Id)
            return bsllight::describeSliceRefiner3();
        if (typeId == bsllight::kVhsId)
            return bsllight::describeVhs();
        if (typeId == bsllight::kRelightId)
            return bsllight::describeRelight();
        if (typeId == bsllight::kBodyGlitchId)
            return bsllight::describeBodyGlitch();
        if (typeId == bsllight::kAuraId)
            return bsllight::describeAura();
        if (typeId == bsllight::kZSliceId)
            return bsllight::describeZSlice();
        return {};
    }
};

} // namespace

std::shared_ptr<IEffectProvider> makeBslProvider()
{
    return std::make_shared<BslProvider>();
}

} // namespace bsl::fx
