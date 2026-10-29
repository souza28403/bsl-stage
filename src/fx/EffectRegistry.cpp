#include "fx/EffectRegistry.h"

namespace bsl::fx {

EffectRegistry& EffectRegistry::instance()
{
    static EffectRegistry reg = [] {
        EffectRegistry r;
        r.registerProvider(makeNativeProvider());
        r.registerProvider(makeBslProvider());
        r.registerProvider(makeIsfProvider());
        r.registerProvider(makeFfglProvider());
        return r;
    }();
    return reg;
}

void EffectRegistry::registerProvider(std::shared_ptr<IEffectProvider> provider)
{
    if (provider)
        providers_.push_back(std::move(provider));
}

const IEffectProvider* EffectRegistry::providerFor(const std::string& typeId) const
{
    for (const auto& p : providers_)
        if (typeId.rfind(p->prefix(), 0) == 0)
            return p.get();
    return nullptr;
}

std::vector<EffectTypeInfo> EffectRegistry::types() const
{
    std::vector<EffectTypeInfo> out;
    for (const auto& p : providers_) {
        std::vector<EffectTypeInfo> l = p->list();
        out.insert(out.end(), std::make_move_iterator(l.begin()), std::make_move_iterator(l.end()));
    }
    return out;
}

std::optional<EffectTypeInfo> EffectRegistry::find(const std::string& typeId) const
{
    const IEffectProvider* p = providerFor(typeId);
    return p ? p->info(typeId) : std::nullopt;
}

std::unique_ptr<IEffect> EffectRegistry::create(const std::string& typeId) const
{
    const IEffectProvider* p = providerFor(typeId);
    return p ? p->create(typeId) : nullptr;
}

std::vector<media::ParamDesc> EffectRegistry::describe(const std::string& typeId) const
{
    const IEffectProvider* p = providerFor(typeId);
    return p ? p->describe(typeId) : std::vector<media::ParamDesc>{};
}

std::uint64_t EffectRegistry::version() const
{
    std::uint64_t v = 0;
    for (const auto& p : providers_)
        v = v * 1000003u + p->version();
    return v;
}

} // namespace bsl::fx
