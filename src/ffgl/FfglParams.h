#pragma once

// Mapeamento dos parametros FFGL para ParamDesc (a UI generica) e de volta. Codigo puro (sem GL nem DLL): testavel.
//
//   standard -> float (faixa do FF_GET_RANGE)   boolean -> bool          event -> botao (contador)
//   integer -> int                              option -> enum           text/file -> texto/arquivo
//   red/green/blue(+alpha) consecutivos -> UMA cor     hue/saturation/brightness(+alpha) -> cor HSB (converte)
//   xpos/ypos consecutivos -> point2D           grupos (FF_GET_PARAM_GROUP) -> secoes da UI

#include "ffgl/FfglInfo.h"
#include "media/SourceParams.h"

#include <string>
#include <vector>

namespace bsl::ffgl {

enum class BindKind { Float, Int, Bool, Event, Option, Text, File, Rgb, Hsb, Point };

// Um ParamDesc da UI e os parametros FFGL que ele controla
struct Binding
{
    BindKind kind = BindKind::Float;
    int params[4] = {-1, -1, -1, -1}; // indices FFGL: Rgb/Hsb = r,g,b[,a] ou h,s,b[,a]; Point = x,y
    int count = 1;                    // quantos de params[] valem
    float min = 0.0f, max = 1.0f;     // Float/Int
    std::vector<OptionInfo> options;  // Option
};

struct ParamMap
{
    std::vector<media::ParamDesc> descs; // o que a UI mostra
    std::vector<Binding> bindings;       // paralelo a descs
    int paramCount = 0;                  // numero de parametros do plugin (inclui os ignorados)
};

ParamMap buildParamMap(const PluginInfo& info);

// Valor de um parametro FFGL como o plugin o recebe/devolve
struct PluginValue
{
    float f = 0.0f;
    std::string s; // texto/arquivo
};

// Parametro FFGL e o valor a enviar
struct IndexedValue
{
    int index = 0;
    PluginValue value;
};

// Os valores que o plugin deve receber para o descritor 'bindingIndex' segundo os valores da UI ('set' usa map.descs).
// Event: f = contador de disparos (quem envia decide o pulso).
void toPlugin(const ParamMap& map, size_t bindingIndex, const media::ParamSet& set, std::vector<IndexedValue>& out);
// O contrario: o valor JSON do descritor a partir dos valores atuais do plugin (indexados como no plugin).
// Event nao tem valor de volta (JSON nulo).
media::Json fromPlugin(const ParamMap& map, size_t bindingIndex, const std::vector<PluginValue>& plugin);
// Valores diferentes (tolerancia de ponto flutuante; texto exato)
bool pluginValueDiffers(const PluginValue& a, const PluginValue& b);

// Valor de uma opcao como texto (a chave gravada no projeto) e o inverso
std::string optionKey(float value);
float optionValue(const std::string& key);

// RGB <-> HSB (tudo em 0..1)
void rgbToHsb(float r, float g, float b, float& h, float& s, float& v);
void hsbToRgb(float h, float s, float v, float& r, float& g, float& b);

} // namespace bsl::ffgl
