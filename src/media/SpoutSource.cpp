#include "media/SpoutSource.h"

#include "media/Frame.h"
#include "media/YuvConverter.h"
#include "render/GlStateGuard.h"

#include <chrono>
#include <mutex>

namespace bsl::media {

namespace {

std::string shortName(std::string name, size_t maxChars)
{
    if (name.size() <= maxChars)
        return name;
    size_t cut = maxChars - 1;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80)
        --cut;
    return name.substr(0, cut) + "…";
}

} // namespace

SpoutSource::SpoutSource() = default;

SpoutSource::~SpoutSource()
{
    close();
}

std::vector<ParamDesc> SpoutSource::describe(const ParamValues& /*values*/)
{
    using namespace param;
    std::vector<ParamDesc> d;
    d.push_back(makeCustom("sender", "Sender Spout", "spout-sender", std::string(), "Entrada"));
    return d;
}

std::string SpoutSource::label(const ParamValues& values)
{
    std::string name;
    if (const Json* v = values.find("sender"); v && v->is_string())
        name = v->get<std::string>();
    if (name.empty())
        return "Spout";
    return "Spout: " + shortName(name, 28);
}

std::vector<std::string> SpoutSource::activeSenders()
{
    using Clock = std::chrono::steady_clock;
    static std::mutex m;
    static std::vector<std::string> cache;
    static Clock::time_point last{};
    static bool have = false;
    std::lock_guard lk(m);
    const auto now = Clock::now();
    if (!have || now - last > std::chrono::seconds(1)) {
        cache = SpoutInput::listSenders();
        last = now;
        have = true;
    }
    return cache;
}

GLuint SpoutSource::texture() const
{
    return (input_.connected() && input_.texture()) ? input_.texture() : blackTex_;
}

bool SpoutSource::open(const std::filesystem::path& /*path*/, std::string* /*error*/)
{
    close();
    clock_ = ClipClock{};
    open_ = true;
    paramsDirty_ = false;
    showBlack();
    wanted_ = currentParams().s("sender");
    input_.setSender(wanted_);
    return true;
}

void SpoutSource::close()
{
    input_.release();
    if (blackTex_) {
        glDeleteTextures(1, &blackTex_);
        blackTex_ = 0;
    }
    blackW_ = blackH_ = 0;
    open_ = false;
}

void SpoutSource::showBlack()
{
    Frame black;
    black.allocate(width_, height_, PixelFormat::RGBA8);
    for (size_t i = 3; i < black.planes[0].size(); i += 4)
        black.planes[0][i] = 255;
    uploadRgbaTexture(blackTex_, blackW_, blackH_, black);
}

void SpoutSource::update(double /*dt*/)
{
    if (!open_)
        return;
    if (paramsDirty_) {
        paramsDirty_ = false;
        const std::string want = currentParams().s("sender");
        if (want != wanted_) {
            wanted_ = want;
            input_.setSender(wanted_);
        }
    }
    {
        GlStateGuard guard; // o Spout mexe em FBOs/texturas próprios
        input_.receive();
    }
    if (input_.connected()) {
        width_ = input_.width();
        height_ = input_.height();
    }
}

SourceStatus SpoutSource::status() const
{
    using Level = SourceStatus::Level;
    if (input_.connected()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), " | %d x %d", input_.width(), input_.height());
        return {Level::Info, "Spout: " + input_.connectedName() + buf};
    }
    if (!wanted_.empty())
        return {Level::Warning, "Sem sinal: esperando o sender \"" + wanted_ + "\""};
    return {Level::Warning, "Sem sinal: nenhum sender Spout ativo"};
}

} // namespace bsl::media
