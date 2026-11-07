#pragma once

#include "fx/IEffect.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx {

struct EffectInstance
{
    std::string typeId;
    bool bypass = false;
    media::ParamValues params;
    bool uiOpen = true;
    int uid = 0;
    bool faulted = false;

    EffectInstance();
    explicit EffectInstance(std::string type);
    EffectInstance(const EffectInstance& o);
    EffectInstance& operator=(const EffectInstance& o);
    EffectInstance(EffectInstance&&) noexcept = default;
    EffectInstance& operator=(EffectInstance&&) noexcept = default;
    ~EffectInstance() = default;

    void setParam(const std::string& id, media::Json value);
    void resetParams();

    std::vector<media::ParamDesc> paramDescs() const;
    std::string displayName() const;
    bool known() const;
    media::SourceStatus status() const;

    IEffect* ensureEffect();
    IEffect* effect() const { return effect_.get(); }
    void dropEffect() { effect_.reset(); }
    void syncParams();
    void pollEffect();

    bool updateFrom(const EffectInstance& o);

    media::Json toJson() const;
    static EffectInstance fromJson(const media::Json& j);

private:
    std::unique_ptr<IEffect> effect_;
    unsigned revision_ = 1;
    unsigned applied_ = 0;
    bool createFailed_ = false;
    std::chrono::steady_clock::time_point lastEdit_{};
    unsigned registryVersion_ = 0;
};

class EffectChain
{
public:
    static constexpr size_t kMaxEffects = 32;

    std::vector<EffectInstance> items;

    bool empty() const { return items.empty(); }
    int size() const { return static_cast<int>(items.size()); }
    bool valid(int index) const { return index >= 0 && index < size(); }
    bool hasActive() const;

    EffectInstance& add(const std::string& typeId);
    EffectInstance& insert(int index, const std::string& typeId);
    bool remove(int index);
    bool move(int from, int to);
    bool duplicate(int index);
    bool setBypass(int index, bool bypass);

    void syncFrom(const EffectChain& src);

    media::Json toJson() const;
    static EffectChain fromJson(const media::Json& j, std::vector<std::string>* warnings = nullptr);
};

}
