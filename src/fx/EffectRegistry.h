#pragma once

#include "fx/IEffect.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::fx {

enum class EffectSection { Native, Isf, Ffgl, Bsl };

struct EffectTypeInfo
{
    std::string id;
    std::string name;
    std::string category;
    const char* icon = "";
    EffectSection section = EffectSection::Native;
    std::string description;
    std::string error;
    bool generator = false;
    std::string sourceFile;
    bool ai = false;
};

class IEffectProvider
{
public:
    virtual ~IEffectProvider() = default;
    virtual std::string prefix() const = 0;
    virtual std::vector<EffectTypeInfo> list() const = 0;
    virtual std::optional<EffectTypeInfo> info(const std::string& typeId) const
    {
        for (EffectTypeInfo& t : list())
            if (t.id == typeId)
                return std::move(t);
        return std::nullopt;
    }
    virtual std::unique_ptr<IEffect> create(const std::string& typeId) const = 0;
    virtual std::vector<media::ParamDesc> describe(const std::string& typeId) const = 0;
    virtual std::uint64_t version() const { return 0; }
};

class EffectRegistry
{
public:
    static EffectRegistry& instance();

    void registerProvider(std::shared_ptr<IEffectProvider> provider);

    std::vector<EffectTypeInfo> types() const;
    std::optional<EffectTypeInfo> find(const std::string& typeId) const;
    std::unique_ptr<IEffect> create(const std::string& typeId) const;
    std::vector<media::ParamDesc> describe(const std::string& typeId) const;
    std::uint64_t version() const;

private:
    EffectRegistry() = default;
    const IEffectProvider* providerFor(const std::string& typeId) const;
    std::vector<std::shared_ptr<IEffectProvider>> providers_;
};

std::shared_ptr<IEffectProvider> makeNativeProvider();
std::shared_ptr<IEffectProvider> makeIsfProvider();
std::shared_ptr<IEffectProvider> makeFfglProvider();
std::shared_ptr<IEffectProvider> makeBslProvider();

}
