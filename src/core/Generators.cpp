#include "core/Generators.h"

#include "media/CaptureDeviceSource.h"
#include "media/ScreenCaptureSource.h"
#include "media/TextSource.h"
#include "media/WebSource.h"
#include "render/SolidColorSource.h"
#include "render/TestPatternSource.h"

namespace bsl {

namespace {

std::vector<media::ParamDesc> noParams(const media::ParamValues&)
{
    return {};
}

const GeneratorInfo kGenerators[] = {
    {kTestPatternId, "Padrão de teste", false, false, &noParams,
     [](int w, int h) -> std::unique_ptr<media::ISource> { return std::make_unique<TestPatternSource>(w, h); }, nullptr},
    {kSolidColorId, "Cor sólida", false, false, &SolidColorSource::describe,
     [](int w, int h) -> std::unique_ptr<media::ISource> { return std::make_unique<SolidColorSource>(w, h); }, nullptr},
    {kTextId, "Texto", false, false, &media::TextSource::describe,
     [](int w, int h) -> std::unique_ptr<media::ISource> { return std::make_unique<media::TextSource>(w, h); },
     &media::TextSource::label},
    {kScreenCaptureId, "Captura de tela", true, true, &media::ScreenCaptureSource::describe,
     [](int, int) -> std::unique_ptr<media::ISource> { return std::make_unique<media::ScreenCaptureSource>(); },
     &media::ScreenCaptureSource::label},
    {kCaptureDeviceId, "Placa de captura / webcam", true, false, &media::CaptureDeviceSource::describe,
     [](int, int) -> std::unique_ptr<media::ISource> { return std::make_unique<media::CaptureDeviceSource>(); },
     &media::CaptureDeviceSource::label},
    {kWebUrlId, "Web (URL)", true, false, &media::WebSource::describe,
     [](int w, int h) -> std::unique_ptr<media::ISource> { return std::make_unique<media::WebSource>(w, h); },
     &media::WebSource::label},
};

} // namespace

std::span<const GeneratorInfo> allGenerators()
{
    return kGenerators;
}

const GeneratorInfo* findGenerator(const std::string& id)
{
    for (const GeneratorInfo& g : kGenerators)
        if (id == g.id)
            return &g;
    return nullptr;
}

} // namespace bsl
