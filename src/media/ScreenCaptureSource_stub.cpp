#include "media/ScreenCaptureSource.h"

#include "media/CaptureTargets.h"
#include "platform/Unavailable.h"

namespace bsl::media {

namespace {

const std::vector<ParamOption> kFps = {{"15", "15 fps"}, {"30", "30 fps"}, {"60", "60 fps"}};

}

struct ScreenCaptureSource::Impl
{
};

ScreenCaptureSource::ScreenCaptureSource() = default;

ScreenCaptureSource::~ScreenCaptureSource() = default;

void ScreenCaptureSource::forceBackend(CaptureBackend) {}

void ScreenCaptureSource::forceCpuPath(bool) {}

std::vector<ParamDesc> ScreenCaptureSource::describe(const ParamValues&)
{
    using namespace param;
    std::vector<ParamDesc> d;
    d.push_back(makeCustom("target", "Alvo", "screen-target", monitorTargetJson(1), "Captura"));
    d.push_back(makeBool("cursor", "Mostrar cursor", true));
    d.push_back(makeEnum("fps", "FPS máx.", kFps, "30"));
    d.push_back(makeBool("borderless", "Sem borda amarela", true));
    d.back().tooltip = "Remove a borda amarela do Windows ao redor do alvo, quando o Windows permitir (11 22H2 ou mais novo).";
    d.push_back(makeInt("regionX", "Região X", 0, 0, 16384, "%d px", "Região"));
    d.push_back(makeInt("regionY", "Região Y", 0, 0, 16384, "%d px"));
    d.push_back(makeInt("regionW", "Região largura", 0, 0, 16384, "%d px"));
    d.back().tooltip = "0 = até o fim do alvo.";
    d.push_back(makeInt("regionH", "Região altura", 0, 0, 16384, "%d px"));
    d.back().tooltip = "0 = até o fim do alvo.";
    return d;
}

std::string ScreenCaptureSource::label(const ParamValues& values)
{
    const ParamSet p(describe(values), values);
    const Json t = p.raw("target");
    if (t.is_object() && t.value("kind", std::string()) == "window") {
        std::string title = t.value("title", std::string());
        if (title.size() > 24) {
            size_t cut = 22;
            while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80)
                --cut;
            title = title.substr(0, cut) + "…";
        }
        return "Captura: " + (title.empty() ? t.value("exe", std::string("janela")) : title);
    }
    const int index = (t.is_object() && t.contains("index") && t["index"].is_number_integer()) ? t["index"].get<int>() : 1;
    return "Captura: monitor " + std::to_string(index);
}

bool ScreenCaptureSource::open(const std::filesystem::path&, std::string* error)
{
    if (error)
        *error = bslplat::unavailableText("Captura de tela");
    return false;
}

void ScreenCaptureSource::close()
{
    open_ = false;
}

void ScreenCaptureSource::onParamsChanged() {}

void ScreenCaptureSource::update(double) {}

GLuint ScreenCaptureSource::texture() const
{
    return 0;
}

int ScreenCaptureSource::width() const
{
    return 0;
}

int ScreenCaptureSource::height() const
{
    return 0;
}

SourceStatus ScreenCaptureSource::status() const
{
    return {SourceStatus::Level::Warning, bslplat::unavailableText("Captura de tela")};
}

std::string ScreenCaptureSource::backendName() const
{
    return {};
}

std::string ScreenCaptureSource::pathName() const
{
    return {};
}

bool ScreenCaptureSource::usesInterop() const
{
    return false;
}

unsigned long long ScreenCaptureSource::frameCount() const
{
    return 0;
}

}
