#pragma once

// ISF (Interactive Shader Format, especificacao aberta da Vidvox): leitura do cabecalho JSON, expressoes dos passes
// e geracao do codigo GLSL 3.30 core a partir do shader ISF (preambulo + reescrita das macros IMG_*). Sem GL: testavel
// isoladamente. A execucao fica em fx/IsfRenderer.

#include "media/SourceParams.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx {

enum class IsfInputType { Float, Bool, Long, Color, Point2D, Event, Image, Audio, AudioFft, Cube, Unknown };

struct IsfInput
{
    std::string name;
    std::string label;
    IsfInputType type = IsfInputType::Unknown;
    double minV = 0.0, maxV = 1.0, defV = 0.0; // float; long usa defV como valor padrao
    bool defBool = false;
    std::vector<long long> values;             // long: VALUES
    std::vector<std::string> labels;           // long: LABELS
    float defColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float defPoint[2] = {0.0f, 0.0f};
    float minPoint[2] = {0.0f, 0.0f};
    float maxPoint[2] = {0.0f, 0.0f};          // 0,0 = nao informado
};

struct IsfPass
{
    std::string target;     // vazio = renderiza na saida
    bool persistent = false;
    bool isFloat = false;
    std::string widthExpr;  // vazio = tamanho da saida
    std::string heightExpr;
};

struct IsfImported
{
    std::string name;
    std::string path; // relativo ao shader
};

struct IsfDoc
{
    std::string name;        // nome do arquivo sem extensao
    std::string description;
    std::string credit;
    std::vector<std::string> categories;
    std::vector<IsfInput> inputs;
    std::vector<IsfPass> passes;
    std::vector<IsfImported> imported;
    std::string fragmentSource; // arquivo .fs inteiro (inclui o comentario JSON)
    std::string vertexSource;   // arquivo .vs, se existir
    std::vector<std::string> warnings; // ex.: audio/cube ignorados

    // Efeito = tem uma entrada de imagem chamada inputImage; senao e um gerador
    bool isEffect() const;
    int passCount() const { return passes.empty() ? 1 : static_cast<int>(passes.size()); }
};

struct IsfParseResult
{
    bool ok = false;
    std::string error; // PT, com a linha do JSON quando possivel
    std::shared_ptr<IsfDoc> doc;
};

// 'fragment' = conteudo do .fs; 'vertex' = conteudo do .vs (vazio se nao ha); 'name' = nome do arquivo sem extensao
IsfParseResult parseIsf(const std::string& fragment, const std::string& vertex, const std::string& name);

// Descritores de parametros (ParamDesc) dos inputs: float, bool, long (Enum), color, point2D (Custom "point2d",
// valor [x, y]) e event (Custom "event", contador inteiro). A imagem inputImage e as demais imagens/audio nao viram parametro.
std::vector<media::ParamDesc> isfParamDescs(const IsfDoc& doc);

// Expressao de largura/altura dos passes: numeros, $WIDTH, $HEIGHT, $<input>, + - * /, parenteses e
// floor/ceil/round/min/max/abs/sqrt/pow. 'variable' devolve false para nomes desconhecidos.
using IsfVariableLookup = std::function<bool(const std::string& name, double& out)>;
bool evalIsfExpression(const std::string& expr, const IsfVariableLookup& variable, double& result);

// Codigo GLSL pronto para compilar
struct IsfSources
{
    std::string vertex;
    std::string fragment;
    int vertexPreambleLines = 0;   // linhas antes do codigo do usuario (para corrigir a linha dos erros)
    int fragmentPreambleLines = 0;
    std::vector<std::string> imageNames; // samplers declarados: inputs de imagem, imported e TARGET dos passes
};
IsfSources buildIsfSources(const IsfDoc& doc);

// Reescreve a sintaxe ISF/GLSL antigo para o GLSL 3.30 (IMG_*, gl_FragColor, texture2D, varying...). Mantem o numero
// de linhas. Exposto para teste.
std::string rewriteIsfSource(const std::string& source, bool fragmentStage);

struct IsfCompileLog
{
    std::string text;  // pronto para mostrar: "Linha N: mensagem" (numeracao do arquivo do usuario)
    int firstLine = 0; // primeira linha de erro no arquivo do usuario (0 = nao identificada)
};
// Corrige a numeracao do log do driver (NVIDIA "0(12)", Mesa/AMD "0:12:") descontando as linhas do preambulo
IsfCompileLog remapGlslLog(const std::string& log, int preambleLines, const char* stage);

} // namespace bsl::fx
