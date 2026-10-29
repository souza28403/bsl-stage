// Provedor "ffgl:" do registro de efeitos: os plugins FFGL do catalogo (ffgl/FfglCatalog). typeId =
// "ffgl:<id de 4 caracteres>:<arquivo>"; ao abrir um projeto o plugin e procurado pelo id e depois pelo nome do arquivo.

#include "ffgl/FfglCatalog.h"
#include "ffgl/FfglInstance.h"
#include "fx/EffectRegistry.h"
#include "ui/Icons.h"

namespace bsl::fx {

namespace {

class FfglProvider final : public IEffectProvider
{
public:
    std::string prefix() const override { return "ffgl:"; }

    static EffectTypeInfo toInfo(const ffgl::CatalogEntry& e)
    {
        const ffgl::PluginInfo& p = e.info;
        EffectTypeInfo t;
        t.id = e.typeId;
        t.name = ffgl::pluginDisplayName(p);
        t.category = e.origin;
        t.icon = ICON_PLUG;
        t.section = EffectSection::Ffgl;
        t.generator = p.type == ffgl::kTypeSource;
        if (p.type == ffgl::kTypeMixer)
            t.error = "Mixer FFGL: ainda não suportado";
        std::string d = p.description;
        auto line = [&](const std::string& label, const std::string& value) {
            if (value.empty())
                return;
            if (!d.empty())
                d += '\n';
            d += label + value;
        };
        line("Autor: ", p.about);
        line("Versão: ", std::to_string(p.versionMajor) + "." + std::to_string(p.versionMinor));
        line("Caminho: ", p.path);
        t.description = std::move(d);
        return t;
    }

    std::vector<EffectTypeInfo> list() const override
    {
        std::vector<EffectTypeInfo> out;
        for (const ffgl::EntryPtr& e : ffgl::Catalog::instance().entries())
            out.push_back(toInfo(*e));
        return out;
    }

    std::optional<EffectTypeInfo> info(const std::string& typeId) const override
    {
        const ffgl::EntryPtr e = ffgl::Catalog::instance().find(typeId);
        if (!e)
            return std::nullopt;
        return toInfo(*e);
    }

    std::unique_ptr<IEffect> create(const std::string& typeId) const override
    {
        const ffgl::EntryPtr e = ffgl::Catalog::instance().find(typeId);
        if (!e || e->info.type == ffgl::kTypeMixer)
            return nullptr;
        return std::make_unique<ffgl::FfglInstance>(e);
    }

    std::vector<media::ParamDesc> describe(const std::string& typeId) const override
    {
        const ffgl::EntryPtr e = ffgl::Catalog::instance().find(typeId);
        return e ? e->map.descs : std::vector<media::ParamDesc>{}; // do cache, sem GL
    }

    std::uint64_t version() const override { return ffgl::Catalog::instance().version(); }
};

} // namespace

std::shared_ptr<IEffectProvider> makeFfglProvider()
{
    return std::make_shared<FfglProvider>();
}

} // namespace bsl::fx
