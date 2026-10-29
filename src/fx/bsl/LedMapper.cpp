// LED Mapper Pro (plugin FFGL "LMPR"): grade de cabinets de LED (colunas x linhas, tamanho e espaco) centralizada na tela,
// em dois tons alternados, com borda branca e vao vermelho. Reescrito do original; o "gravar XML no Desktop" virou o
// botao "Exportar..." (dialogo de salvar) que grava um AdvancedOutput.xml do Resolume com um slice por cabinet.

#include "fx/bsl/BslShaderSource.h"
#include "fx/bsl/BslSources.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace bsl::fx::bsllight {

namespace {

using media::ParamSet;
using namespace media::param;

// Port do fragment shader original (GLSL 4.10 -> 3.30). A tela tem y para CIMA, como no OpenGL do plugin.
const char* const kLedBody = R"(
uniform float columns;
uniform float rows;
uniform float cabW;
uniform float cabH;
uniform float gapX;
uniform float gapY;

void main()
{
    vec2 pos = glUv() * uSize;
    vec2 resolution = uSize;

    float totalW = cabW + gapX;
    float totalH = cabH + gapY;

    float panelWidth = columns * cabW + (columns - 1.0) * gapX;
    float panelHeight = rows * cabH + (rows - 1.0) * gapY;

    vec2 offset = (resolution - vec2(panelWidth, panelHeight)) * 0.5;
    vec2 localPos = pos - offset;

    if (localPos.x < 0.0 || localPos.x > panelWidth || localPos.y < 0.0 || localPos.y > panelHeight) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float cX = floor(localPos.x / totalW);
    float cY = floor(localPos.y / totalH);
    vec2 cellPos = vec2(mod(localPos.x, totalW), mod(localPos.y, totalH));

    if (cellPos.x > cabW || cellPos.y > cabH) {
        fragColor = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    float checker = mod(cX + cY, 2.0);
    vec3 col = checker > 0.5 ? vec3(0.2, 0.5, 0.8) : vec3(0.2, 0.8, 0.5);

    if (cellPos.x < 2.0 || cellPos.x > cabW - 2.0 || cellPos.y < 2.0 || cellPos.y > cabH - 2.0)
        col = vec3(1.0);

    fragColor = vec4(col, 1.0);
}
)";

LedGrid gridFrom(const ParamSet& p)
{
    LedGrid g;
    g.columns = p.i("columns");
    g.rows = p.i("rows");
    g.cabinetW = p.f("cabinetW");
    g.cabinetH = p.f("cabinetH");
    g.gapX = p.f("gapX");
    g.gapY = p.f("gapY");
    return g;
}

int exportCounter(const ParamSet& p)
{
    const media::Json v = p.raw("export");
    if (v.is_object() && v.contains("n") && v["n"].is_number())
        return v["n"].get<int>();
    return 0;
}

class LedMapperSource final : public BslShaderSource
{
public:
    using BslShaderSource::BslShaderSource;
    std::vector<media::ParamDesc> paramDescs() const override { return describeLedMapper(); }

protected:
    std::string fragmentBody() const override { return kLedBody; }

    void onOpened() override { lastExport_ = exportCounter(currentParams()); }

    // O botao "Exportar..." (ParamWidgets) soma 1 a export.n e grava o caminho escolhido em export.path
    void onParamsChanged() override
    {
        if (!isOpen())
            return; // antes de abrir so entregam os valores salvos do projeto: nunca exporta
        const ParamSet p = currentParams();
        const int n = exportCounter(p);
        if (n == lastExport_)
            return;
        lastExport_ = n;
        const media::Json v = p.raw("export");
        const std::string path = v.is_object() && v.contains("path") && v["path"].is_string() ? v["path"].get<std::string>() : "";
        if (path.empty())
            return;
        std::string error;
        const std::filesystem::path file{std::u8string(path.begin(), path.end())};
        if (writeLedMapperXml(file, gridFrom(p), &error)) {
            message_ = "XML gravado em " + path;
            log::info("LED Mapper Pro: " + message_);
        } else {
            message_ = "Não foi possível gravar o XML: " + error;
            log::warn("LED Mapper Pro: " + message_);
        }
    }

    std::string extraStatus() const override { return message_; }

    void setUniforms(Shader& s, double) override
    {
        const ParamSet p = currentParams();
        const LedGrid g = gridFrom(p);
        s.setFloat("columns", static_cast<float>(g.columns));
        s.setFloat("rows", static_cast<float>(g.rows));
        s.setFloat("cabW", g.cabinetW);
        s.setFloat("cabH", g.cabinetH);
        s.setFloat("gapX", g.gapX);
        s.setFloat("gapY", g.gapY);
    }

private:
    int lastExport_ = 0;
    std::string message_;
};

} // namespace

std::vector<media::ParamDesc> describeLedMapper()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeInt("columns", "Colunas", 1, 1, 50, "%d"));
    d.push_back(makeInt("rows", "Linhas", 1, 1, 50, "%d"));
    d.push_back(makeFloat("cabinetW", "Largura do cabinet", 128.0f, 0.0f, 2000.0f, "%.0f px"));
    d.push_back(makeFloat("cabinetH", "Altura do cabinet", 128.0f, 0.0f, 2000.0f, "%.0f px"));
    d.push_back(makeFloat("gapX", "Espaço horizontal", 0.0f, 0.0f, 100.0f, "%.0f px"));
    d.push_back(makeFloat("gapY", "Espaço vertical", 0.0f, 0.0f, 100.0f, "%.0f px"));
    d.push_back(makeCustom("export", "Exportar XML", "bsl-export", media::Json{{"n", 0}, {"path", ""}}, "Exportar"));
    d.back().tooltip = "Grava um AdvancedOutput.xml do Resolume com um slice por cabinet (InputRect = OutputRect)";
    return d;
}

std::unique_ptr<media::ISource> makeLedMapper(int canvasW, int canvasH)
{
    return std::make_unique<LedMapperSource>(canvasW, canvasH);
}

std::string buildLedMapperXml(const LedGrid& grid, std::uint32_t seed)
{
    std::ostringstream o;
    o.precision(9);
    // uniqueId: 13 digitos como os do Resolume; deterministico a partir da semente
    std::uint32_t state = seed ? seed : 1u;
    auto nextId = [&state]() {
        state = state * 1664525u + 1013904223u;
        const std::uint64_t hi = state;
        state = state * 1664525u + 1013904223u;
        return 1000000000000ull + ((hi << 16) ^ state) % 900000000000ull;
    };

    o << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    o << "<ScreenSetup name=\"ScreenSetup\">\n";
    o << "\t<versionInfo name=\"Resolume Arena\" majorVersion=\"7\" minorVersion=\"20\" microVersion=\"0\" revision=\"0\"/>\n";
    o << "\t<screens>\n";
    o << "\t\t<Screen name=\"LED Mapper Output\" uniqueId=\"" << nextId() << "\">\n";
    o << "\t\t\t<layers>\n";
    for (int y = 0; y < grid.rows; ++y) {
        for (int x = 0; x < grid.columns; ++x) {
            const float left = static_cast<float>(x) * (grid.cabinetW + grid.gapX);
            const float top = static_cast<float>(y) * (grid.cabinetH + grid.gapY);
            const float right = left + grid.cabinetW;
            const float bottom = top + grid.cabinetH;
            o << "\t\t\t\t<Slice uniqueId=\"" << nextId() << "\">\n";
            o << "\t\t\t\t\t<Params name=\"Common\">\n";
            o << "\t\t\t\t\t\t<Param name=\"Name\" T=\"STRING\" default=\"Layer\" value=\"Cabinet " << (x + 1) << "-" << (y + 1)
              << "\"/>\n";
            o << "\t\t\t\t\t</Params>\n";
            for (const char* rect : {"InputRect", "OutputRect"}) {
                o << "\t\t\t\t\t<" << rect << " orientation=\"0\">\n";
                o << "\t\t\t\t\t\t<v x=\"" << left << "\" y=\"" << top << "\"/>\n";
                o << "\t\t\t\t\t\t<v x=\"" << right << "\" y=\"" << top << "\"/>\n";
                o << "\t\t\t\t\t\t<v x=\"" << right << "\" y=\"" << bottom << "\"/>\n";
                o << "\t\t\t\t\t\t<v x=\"" << left << "\" y=\"" << bottom << "\"/>\n";
                o << "\t\t\t\t\t</" << rect << ">\n";
            }
            o << "\t\t\t\t</Slice>\n";
        }
    }
    o << "\t\t\t</layers>\n";
    o << "\t\t</Screen>\n";
    o << "\t</screens>\n";
    o << "</ScreenSetup>\n";
    return o.str();
}

bool writeLedMapperXml(const std::filesystem::path& file, const LedGrid& grid, std::string* error)
{
    const auto seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string xml = buildLedMapperXml(grid, seed);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error)
            *error = "não foi possível abrir " + paths::toUtf8(file);
        return false;
    }
    out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    out.flush();
    if (!out) {
        if (error)
            *error = "falha ao gravar " + paths::toUtf8(file);
        return false;
    }
    return true;
}

} // namespace bsl::fx::bsllight
