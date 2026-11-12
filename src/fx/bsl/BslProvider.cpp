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
        {kSliceRefinerId, "BSL Slice Refiner 2", "Slices",
         "Desenha molduras nos slices da saída (11 modos: neon, gradiente, radar, perseguidor, pulsante, máscara de "
         "vídeo, brilhos, glow, onda RGB, pontilhado e glitch), até 8 slices. Usa os slices do próprio BSL Stage; também "
         "pode importar um arquivo XML de slices.",
         false},
        {kSliceRefiner3Id, "BSL Slice Refiner", "Slices",
         "Recorta o vídeo do clip nos formatos dos slices da saída, em até 8 grupos: cantos arredondados, slices unidos "
         "sem linha no meio, borda com 12 efeitos (manchas, espiral, ondas, zebra, disco, corrida, tracejado, arco-íris, "
         "brilhos, glitch, pulsar, scanner), piscar, bounce no BPM e giro de cor. Usa os slices do próprio BSL Stage; "
         "também pode importar um arquivo XML de slices. O painel de controle é nativo; com Controle remoto pela "
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
        {kSequenciaId, "Sequência de slices", "Sequências",
         "Acende os slices passo a passo, seguindo uma sequência criada no modo Sequências. Avança por velocidade, BPM "
         "ou passo fixo, com direção, ecos e curva de sustentação ou envelope. A escala escolhe se o vídeo aparece no "
         "lugar de cada slice ou esticado em cada um.",
         false},
        {kDisparoId, "Disparo manual", "Sequências",
         "Avança a sequência quando você manda: botões Próximo e Anterior, rajada enquanto estiver ativa e reinício "
         "automático depois de um tempo parado. Cada passo apaga com sustentação e liberação ou com um envelope.",
         false},
        {kAtrasoSlicesId, "Atraso por slice", "Sequências",
         "Atrasa cada slice por um tempo diferente, seguindo os passos de uma sequência: o passo k atrasa k vezes a "
         "unidade. Guarda um histórico reduzido do vídeo, com memória máxima ajustável.",
         false},
        {kSlicesAleatoriosId, "Slices aleatórios", "Sequências",
         "Sorteia alguns slices de uma visão a cada passo, sem repetir os do passo anterior, com ecos, semente fixa e "
         "curva de sustentação ou envelope.",
         false},
        {kContornoId, "Contorno", "Sequências",
         "Desenha o contorno dos slices de uma visão, com cor e espessura, e pode animar um trecho de luz correndo pelo "
         "perímetro, com leque entre os slices, direção e quantidade de trechos.",
         false},
        {kLadrilhosId, "Ladrilhos", "Sequências",
         "Preenche os slices de uma visão com ladrilhos coloridos ou com a textura do vídeo, em quadrado ou círculo, "
         "com grade, fase e velocidade.",
         false},
        {kAmostradorId, "Amostrador de slices", "Sequências",
         "Lê a cor do vídeo no centro ou na média de cada slice e pinta o slice inteiro com ela, com ganho e suavização "
         "no tempo.",
         false},
        {kVarreduraId, "Varredura", "Sequências",
         "Varre cada slice de uma sequência com uma borda de luz que corre de lado a lado, com sobreposição entre "
         "passos, voltas e envelope.",
         false},
        {kBordaNeonId, "Borda neon", "Sequências",
         "Borda de neon que percorre um retângulo (ou os slices de uma visão) em segmentos, com padrões, barras "
         "laterais, brilho e flash no compasso.",
         false},
        {kGeradorContornoId, "Contorno", "Sequências",
         "Gerador: desenha o contorno dos slices de uma visão sobre fundo transparente, com cor, espessura e um trecho "
         "de luz que corre pelo perímetro.",
         true},
        {kGeradorLadrilhosId, "Ladrilhos", "Sequências",
         "Gerador: preenche os slices de uma visão com ladrilhos coloridos, em quadrado ou círculo, com grade, fase e "
         "velocidade, sobre fundo transparente.",
         true},
        {kGeradorBordaNeonId, "Borda neon", "Sequências",
         "Gerador: borda de neon que percorre um retângulo (ou os slices de uma visão) em segmentos, com padrões, barras "
         "laterais, brilho e flash no compasso, sobre fundo transparente.",
         true},
        {kWebBackgroundsId, "BSL WebBackgrounds", "Fundos",
         "Mais de 40 fundos generativos ao vivo (Silk, Aurora, Galaxy, Balatro...), paletas, efeitos de imagem (espelho, "
         "pixel, LED, CRT, strobo no BPM), texto e logo animados com entrada, loop e saída. Roda a página do BSL Studio "
         "na fonte Web do app; todos os controles são parâmetros do clip e também podem ser mexidos pelo painel de "
         "controle (botão \"Abrir painel de controle\" no painel Clip).",
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

}

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
    if (id == kWebBackgroundsId) {
        node->info.live = true;
        node->info.describe = [](const media::ParamValues& v) { return describeWebBackgrounds(v); };
        node->info.create = [](int w, int h) { return makeWebBackgrounds(w, h); };
    } else if (id == kPixelMapPatternId) {
        node->info.describe = [](const media::ParamValues&) { return describePixelMapPattern(); };
        node->info.create = [](int w, int h) { return makePixelMapPattern(w, h); };
    } else if (id == kGeradorContornoId) {
        node->info.describe = [](const media::ParamValues&) { return describeGeradorContorno(); };
        node->info.create = [](int w, int h) { return makeGeradorContorno(w, h); };
    } else if (id == kGeradorLadrilhosId) {
        node->info.describe = [](const media::ParamValues&) { return describeGeradorLadrilhos(); };
        node->info.create = [](int w, int h) { return makeGeradorLadrilhos(w, h); };
    } else if (id == kGeradorBordaNeonId) {
        node->info.describe = [](const media::ParamValues&) { return describeGeradorBordaNeon(); };
        node->info.create = [](int w, int h) { return makeGeradorBordaNeon(w, h); };
    } else {
        return nullptr;
    }
    const char* name = plugin->name;
    node->info.label = [name](const media::ParamValues&) { return std::string(name); };
    const GeneratorInfo* result = &node->info;
    nodes.emplace(id, std::move(node));
    return result;
}

}

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
        if (typeId == bsllight::kSequenciaId)
            return bsllight::makeSequencia();
        if (typeId == bsllight::kDisparoId)
            return bsllight::makeDisparo();
        if (typeId == bsllight::kAtrasoSlicesId)
            return bsllight::makeAtrasoSlices();
        if (typeId == bsllight::kSlicesAleatoriosId)
            return bsllight::makeSlicesAleatorios();
        if (typeId == bsllight::kContornoId)
            return bsllight::makeContorno();
        if (typeId == bsllight::kLadrilhosId)
            return bsllight::makeLadrilhos();
        if (typeId == bsllight::kAmostradorId)
            return bsllight::makeAmostrador();
        if (typeId == bsllight::kVarreduraId)
            return bsllight::makeVarredura();
        if (typeId == bsllight::kBordaNeonId)
            return bsllight::makeBordaNeon();
        return nullptr;
    }

    std::vector<media::ParamDesc> describe(const std::string& typeId) const override
    {
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
        if (typeId == bsllight::kSequenciaId)
            return bsllight::describeSequencia();
        if (typeId == bsllight::kDisparoId)
            return bsllight::describeDisparo();
        if (typeId == bsllight::kAtrasoSlicesId)
            return bsllight::describeAtrasoSlices();
        if (typeId == bsllight::kSlicesAleatoriosId)
            return bsllight::describeSlicesAleatorios();
        if (typeId == bsllight::kContornoId)
            return bsllight::describeContorno();
        if (typeId == bsllight::kLadrilhosId)
            return bsllight::describeLadrilhos();
        if (typeId == bsllight::kAmostradorId)
            return bsllight::describeAmostrador();
        if (typeId == bsllight::kVarreduraId)
            return bsllight::describeVarredura();
        if (typeId == bsllight::kBordaNeonId)
            return bsllight::describeBordaNeon();
        return {};
    }
};

}

std::shared_ptr<IEffectProvider> makeBslProvider()
{
    return std::make_shared<BslProvider>();
}

}
