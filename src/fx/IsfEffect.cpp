#include "fx/EffectRegistry.h"
#include "fx/FxGl.h"
#include "fx/IsfLibrary.h"
#include "fx/IsfReload.h"
#include "fx/IsfRenderer.h"
#include "ui/Icons.h"

#include "app/Log.h"
#include "app/Paths.h"

namespace bsl::fx {

namespace {

class IsfEffect final : public IEffect
{
public:
    explicit IsfEffect(std::string typeId) : typeId_(std::move(typeId)) { refresh(true); }

    std::string id() const override { return typeId_; }
    std::string name() const override { return name_; }
    std::string category() const override { return category_; }
    std::vector<media::ParamDesc> paramDescs() const override { return descs_; }

    void setParams(const media::ParamSet& params) override
    {
        values_ = params.values();
        if (renderer_)
            renderer_->setParams(media::ParamSet(descs_, values_));
    }

    void process(const EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output) override
    {
        refresh(false, true);
        if (renderer_ && renderer_->render(ctx, inputTex, w, h, w, h, output))
            return;
        if (inputTex != 0)
            blit_.copy(inputTex, w, h, output);
    }

    media::SourceStatus status() const override
    {
        if (!renderer_)
            return {media::SourceStatus::Level::Warning, "Shader ISF não encontrado: " + typeId_};
        if (!reload_.error.empty())
            return {media::SourceStatus::Level::Warning, isfReloadMessage(reload_)};
        if (!renderer_->error().empty())
            return {media::SourceStatus::Level::Warning, renderer_->error()};
        if (!renderer_->warnings().empty())
            return {media::SourceStatus::Level::Info, renderer_->warnings().front()};
        return {};
    }

private:
    void refresh(bool force, bool glReady = false)
    {
        IsfLibrary& lib = IsfLibrary::instance();
        const std::uint64_t v = lib.version();
        if (!force && v == seenVersion_)
            return;
        seenVersion_ = v;
        const auto entry = lib.find(typeId_);
        if (isfReload(renderer_, entry, glReady, true, reload_) != IsfReloadResult::Replaced)
            return;
        name_ = entry->name;
        category_ = entry->category;
        descs_ = isfParamDescs(*entry->doc);
        renderer_->setParams(media::ParamSet(descs_, values_));
    }

    std::string typeId_;
    std::string name_;
    std::string category_;
    std::vector<media::ParamDesc> descs_;
    media::ParamValues values_;
    std::unique_ptr<IsfRenderer> renderer_;
    IsfReloadState reload_;
    std::uint64_t seenVersion_ = 0;
    TexBlitter blit_;
};

class IsfProvider final : public IEffectProvider
{
public:
    std::string prefix() const override { return "isf:"; }

    static EffectTypeInfo toInfo(const IsfEntry& e)
    {
        EffectTypeInfo t;
        t.id = e.id;
        t.name = e.name;
        t.category = e.category;
        t.icon = ICON_FILE_CODE;
        t.section = EffectSection::Isf;
        t.description = e.description;
        t.error = e.error;
        t.sourceFile = paths::toUtf8(e.file);
        t.generator = e.error.empty() && !e.isEffect;
        return t;
    }

    std::vector<EffectTypeInfo> list() const override
    {
        std::vector<EffectTypeInfo> out;
        for (const IsfEntry& e : IsfLibrary::instance().entries())
            out.push_back(toInfo(e));
        return out;
    }

    std::optional<EffectTypeInfo> info(const std::string& typeId) const override
    {
        const auto e = IsfLibrary::instance().find(typeId);
        if (!e)
            return std::nullopt;
        return toInfo(*e);
    }

    std::unique_ptr<IEffect> create(const std::string& typeId) const override
    {
        const auto e = IsfLibrary::instance().find(typeId);
        if (!e || !e->doc || !e->isEffect)
            return nullptr;
        return std::make_unique<IsfEffect>(typeId);
    }

    std::vector<media::ParamDesc> describe(const std::string& typeId) const override
    {
        const auto doc = IsfLibrary::instance().doc(typeId);
        return doc ? isfParamDescs(*doc) : std::vector<media::ParamDesc>{};
    }

    std::uint64_t version() const override { return IsfLibrary::instance().version(); }
};

}

std::shared_ptr<IEffectProvider> makeIsfProvider()
{
    return std::make_shared<IsfProvider>();
}

}
