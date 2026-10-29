#pragma once

#include "core/Generators.h"
#include "fx/EffectChain.h"
#include "media/ISource.h"
#include "media/MediaFormats.h"
#include "render/Texture.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl {

enum class FitMode { Fit, Fill, Stretch, Original };
inline constexpr int kFitModeCount = 4;

inline const char* fitModeName(FitMode m)
{
    static const char* const names[kFitModeCount] = {"Ajustar", "Preencher", "Esticar", "Original"};
    return names[static_cast<int>(m)];
}

inline const char* loopModeName(media::LoopMode m)
{
    switch (m) {
    case media::LoopMode::Loop: return "Loop";
    case media::LoopMode::Once: return "Uma vez";
    case media::LoopMode::PingPong: return "Ping-pong";
    }
    return "";
}

// Celula da grade. Os parametros de reproducao ficam aqui (valem mesmo com a fonte fechada);
// a fonte so existe enquanto o clip esta disparado numa camada.
struct Clip
{
    enum class Type { Empty, File, Generator };

    Type type = Type::Empty;
    std::string name;               // nome exibido (UTF-8)
    std::filesystem::path path;     // arquivo (vazio no gerador)
    std::string generatorId;        // Type::Generator: "gen:..." (ver core/Generators.h)
    media::ParamValues params;      // parametros da fonte (id -> valor); valem mesmo com a fonte fechada
    media::MediaKind kind = media::MediaKind::Unknown;
    bool missing = false;           // arquivo nao encontrado ao abrir o projeto (a celula nao dispara)

    // Parametros salvos no clip
    float speed = 1.0f;
    media::LoopMode loop = media::LoopMode::Loop;
    FitMode fit = FitMode::Fit;
    bool playing = true;
    bool interact = false;          // Web: encaminha mouse/teclado da Previa para a pagina (nao e salvo)

    fx::EffectChain effects;        // cadeia de efeitos do clip (aplicada no tamanho da fonte, antes do encaixe)

    std::unique_ptr<media::ISource> source; // aberta so quando disparado

    // Miniatura de fonte ao vivo/gerada, atualizada a partir da fonte enquanto ela esta ativa (no maximo 1x/s)
    Texture liveThumb;
    double liveThumbTime = -1.0e9; // relogio da UI do ultimo desenho

    bool empty() const { return type == Type::Empty; }
    bool isOpen() const { return source && source->isOpen(); }
    // Videos e fontes ao vivo so tocam para frente: sem ping-pong
    bool supportsPingPong() const { return kind != media::MediaKind::Video && !isLive() && !hasPageNav(); }

    bool isGenerator() const { return type == Type::Generator; }
    bool isTestPattern() const { return type == Type::Generator && generatorId == kTestPatternId; }
    const GeneratorInfo* generatorInfo() const { return type == Type::Generator ? findGenerator(generatorId) : nullptr; }
    // Fonte ao vivo (dispositivo/captura/web): exclusiva, sem transporte
    bool isLive() const;
    // Fonte que vive num processo auxiliar (PDF, Office, Web)
    bool isRemote() const;
    bool isWeb() const;   // gerador Web (URL) ou arquivo .html
    bool isPaged() const; // PDF ou documento do Office (paginas)
    // Com navegacao por pagina: PDF/Office e a fonte BSL PPT Manager (um gerador que abre um documento)
    bool hasPageNav() const { return isPaged() || generatorId == kBslPptManagerId; }
    // A Previa nunca abre uma segunda instancia (ao vivo ou remota)
    bool isExclusive() const { return isLive() || hasPageNav(); }
    // Parametros da fonte (vazio nos arquivos e no padrao de teste). Funciona com a fonte fechada.
    std::vector<media::ParamDesc> paramDescs() const;
    // Grava o valor no clip e avisa a fonte aberta (onParamsChanged); atualiza o nome exibido
    void setParam(const std::string& id, media::Json value);
    void refreshName();

    static Clip fromFile(const std::filesystem::path& file);
    static Clip testPattern();
    // Gerador com os parametros no padrao. Clip vazio se o id for desconhecido.
    static Clip generator(const std::string& id);
    // Aceita o caminho de um arquivo ou o id de um gerador ("gen:..."). Clip vazio se nao for suportado.
    static Clip fromId(const std::string& idOrPath);

    // Aplicam o parametro ao clip e, se a fonte estiver aberta, a ela
    void setSpeed(float s);
    void setLoop(media::LoopMode m);
    void setPlaying(bool p);
    void restart();
    void seek(double seconds);
};

} // namespace bsl
