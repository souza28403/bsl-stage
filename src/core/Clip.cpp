#include "core/Clip.h"

#include "app/Paths.h"
#include "media/DocInfo.h"
#include "media/PdfSource.h"
#include "media/WebSource.h"

#include <algorithm>

namespace bsl {

Clip Clip::fromFile(const std::filesystem::path& file)
{
    Clip c;
    const media::MediaKind kind = media::classify(file);
    if (kind == media::MediaKind::Unknown)
        return c;
    c.type = Type::File;
    c.kind = kind;
    c.path = file;
    c.name = paths::toUtf8(file.filename());
    c.params.fillDefaults(c.paramDescs()); // PDF/Office/Web: parametros no padrao
    return c;
}

Clip Clip::testPattern()
{
    return generator(kTestPatternId);
}

Clip Clip::generator(const std::string& id)
{
    Clip c;
    const GeneratorInfo* info = findGenerator(id);
    if (!info)
        return c;
    c.type = Type::Generator;
    c.generatorId = id;
    c.params.fillDefaults(info->describe(c.params));
    c.refreshName();
    return c;
}

bool Clip::isLive() const
{
    const GeneratorInfo* info = generatorInfo();
    return (info && info->live) || (type == Type::File && kind == media::MediaKind::Web);
}

bool Clip::isRemote() const
{
    return (type == Type::File && media::isRemoteKind(kind)) || generatorId == kWebUrlId ||
           generatorId == kBslWebBackgroundsId || generatorId == kBslWebMotionId || generatorId == kBslPptManagerId;
}

bool Clip::isWeb() const
{
    return (type == Type::File && kind == media::MediaKind::Web) || generatorId == kWebUrlId ||
           generatorId == kBslWebBackgroundsId || generatorId == kBslWebMotionId;
}

bool Clip::isPaged() const
{
    return type == Type::File && media::isPagedKind(kind);
}

std::vector<media::ParamDesc> Clip::paramDescs() const
{
    if (const GeneratorInfo* info = generatorInfo())
        return info->describe(params);
    if (type == Type::File) {
        if (source && media::isRemoteKind(kind))
            return source->paramDescs(); // com o total de paginas real
        if (media::isPagedKind(kind))
            return media::PdfSource::describe(params, media::docPageCount(path));
        if (kind == media::MediaKind::Web)
            return media::WebSource::describeFile(params);
    }
    return {};
}

void Clip::refreshName()
{
    if (const GeneratorInfo* info = generatorInfo()) {
        name = info->label ? info->label(params) : info->name;
    } else if (isPaged()) {
        // "label": nome gravado por "Expandir paginas na linha" (cada clip fixo na sua pagina)
        if (const media::Json* v = params.find("label"); v && v->is_string() && !v->get<std::string>().empty())
            name = v->get<std::string>();
    }
}

void Clip::setParam(const std::string& id, media::Json value)
{
    params.set(id, std::move(value));
    if (source)
        source->setParams(params);
    refreshName();
}

Clip Clip::fromId(const std::string& idOrPath)
{
    if (idOrPath.rfind("gen:", 0) == 0 || isIsfGeneratorId(idOrPath) || isFfglGeneratorId(idOrPath) ||
        isBslGeneratorId(idOrPath))
        return generator(idOrPath);
    return fromFile(std::filesystem::path(std::u8string(idOrPath.begin(), idOrPath.end())));
}

void Clip::setSpeed(float s)
{
    speed = std::clamp(s, 0.0f, 4.0f);
    if (source)
        source->setSpeed(speed);
}

void Clip::setLoop(media::LoopMode m)
{
    if (m == media::LoopMode::PingPong && !supportsPingPong())
        m = media::LoopMode::Loop;
    loop = m;
    if (source)
        source->setLoopMode(loop);
}

void Clip::setPlaying(bool p)
{
    playing = p;
    if (source) {
        if (p)
            source->play();
        else
            source->pause();
    }
}

void Clip::restart()
{
    if (source) {
        source->seek(0.0);
        source->play();
    }
    playing = true;
}

void Clip::seek(double seconds)
{
    if (source)
        source->seek(seconds);
}

} // namespace bsl
