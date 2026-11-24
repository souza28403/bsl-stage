#include "fx/EffectChain.h"

#include "app/Log.h"
#include "fx/EffectRegistry.h"
#include "fx/bsl/SequenceFeed.h"

#include <algorithm>
#include <atomic>

namespace bsl::fx {

namespace {
std::atomic<int> g_nextUid{1};
}

EffectInstance::EffectInstance() : uid(g_nextUid++) {}

EffectInstance::EffectInstance(std::string type) : typeId(std::move(type)), uid(g_nextUid++) {}

EffectInstance::EffectInstance(const EffectInstance& o)
    : typeId(o.typeId), bypass(o.bypass), params(o.params), uiOpen(o.uiOpen), uid(g_nextUid++)
{
}

EffectInstance& EffectInstance::operator=(const EffectInstance& o)
{
    if (this != &o) {
        typeId = o.typeId;
        bypass = o.bypass;
        params = o.params;
        uiOpen = o.uiOpen;
        uid = g_nextUid++;
        effect_.reset();
        revision_ = 1;
        applied_ = 0;
        createFailed_ = false;
        registryVersion_ = 0;
    }
    return *this;
}

void EffectInstance::setParam(const std::string& id, media::Json value)
{
    params.set(id, std::move(value));
    ++revision_;
    lastEdit_ = std::chrono::steady_clock::now();
}

void EffectInstance::resetParams()
{
    params = media::ParamValues();
    ++revision_;
    lastEdit_ = std::chrono::steady_clock::now();
}

std::vector<media::ParamDesc> EffectInstance::paramDescs() const
{
    if (effect_)
        return effect_->paramDescs();
    return EffectRegistry::instance().describe(typeId);
}

std::string EffectInstance::displayName() const
{
    if (const auto info = EffectRegistry::instance().find(typeId))
        return info->name;
    const size_t colon = typeId.find(':');
    return colon == std::string::npos ? typeId : typeId.substr(colon + 1);
}

bool EffectInstance::known() const
{
    const auto info = EffectRegistry::instance().find(typeId);
    return info && info->error.empty();
}

media::SourceStatus EffectInstance::status() const
{
    if (effect_) {
        media::SourceStatus st = effect_->status();
        if (!st.text.empty())
            return st;
    }
    const auto info = EffectRegistry::instance().find(typeId);
    if (!info)
        return {media::SourceStatus::Level::Warning, "Efeito não encontrado: " + typeId + " (ignorado)"};
    if (!info->error.empty())
        return {media::SourceStatus::Level::Warning, info->error};
    return {};
}

IEffect* EffectInstance::ensureEffect()
{
    if (effect_)
        return effect_.get();
    const unsigned version = static_cast<unsigned>(EffectRegistry::instance().version());
    if (createFailed_ && version == registryVersion_)
        return nullptr;
    effect_ = EffectRegistry::instance().create(typeId);
    createFailed_ = effect_ == nullptr;
    registryVersion_ = version;
    applied_ = 0;
    if (!effect_)
        log::warn("Efeito não pôde ser criado: " + typeId);
    return effect_.get();
}

void EffectInstance::syncParams()
{
    if (!effect_ || applied_ == revision_)
        return;
    effect_->setParams(media::ParamSet(effect_->paramDescs(), params));
    applied_ = revision_;
}

void EffectInstance::pollEffect()
{
    if (!effect_ || applied_ != revision_)
        return;
    using namespace std::chrono;
    if (steady_clock::now() - lastEdit_ < milliseconds(400))
        return;
    effect_->pollParamChanges(params);
}

bool EffectInstance::updateFrom(const EffectInstance& o)
{
    if (typeId != o.typeId)
        return false;
    bypass = o.bypass;
    if (params != o.params) {
        params = o.params;
        ++revision_;
    }
    return true;
}

media::Json EffectInstance::toJson() const
{
    media::Json j = media::Json::object();
    j["type"] = typeId;
    j["bypass"] = bypass;
    if (!params.empty())
        j["params"] = params.json();
    return j;
}

EffectInstance EffectInstance::fromJson(const media::Json& j)
{
    EffectInstance e;
    if (!j.is_object())
        return e;
    if (const auto it = j.find("type"); it != j.end() && it->is_string())
        e.typeId = it->get<std::string>();
    if (const auto it = j.find("bypass"); it != j.end() && it->is_boolean())
        e.bypass = it->get<bool>();
    if (const auto it = j.find("params"); it != j.end() && it->is_object())
        e.params = media::ParamValues(*it);
    return e;
}

bool EffectChain::hasActive() const
{
    return std::any_of(items.begin(), items.end(), [](const EffectInstance& e) { return !e.bypass; });
}

EffectInstance& EffectChain::add(const std::string& typeId)
{
    return insert(size(), typeId);
}

EffectInstance& EffectChain::insert(int index, const std::string& typeId)
{
    if (index < 0 || index > size())
        index = size();
    EffectInstance& inst = *items.insert(items.begin() + index, EffectInstance(typeId));
    for (const media::ParamDesc& d : EffectRegistry::instance().describe(typeId)) {
        if (d.custom != "seq-ref" || inst.params.has(d.id))
            continue;
        if (const int id = bsllight::SequenceFeed::instance().sequenciaPadrao(); id > 0)
            inst.setParam(d.id, media::Json(id));
    }
    return inst;
}

bool EffectChain::remove(int index)
{
    if (!valid(index))
        return false;
    items.erase(items.begin() + index);
    return true;
}

bool EffectChain::move(int from, int to)
{
    if (!valid(from) || !valid(to))
        return false;
    if (from == to)
        return true;
    if (from < to)
        std::rotate(items.begin() + from, items.begin() + from + 1, items.begin() + to + 1);
    else
        std::rotate(items.begin() + to, items.begin() + from, items.begin() + from + 1);
    return true;
}

bool EffectChain::duplicate(int index)
{
    if (!valid(index))
        return false;
    EffectInstance copy = items[static_cast<size_t>(index)];
    items.insert(items.begin() + index + 1, std::move(copy));
    return true;
}

bool EffectChain::setBypass(int index, bool bypass)
{
    if (!valid(index))
        return false;
    items[static_cast<size_t>(index)].bypass = bypass;
    return true;
}

void EffectChain::syncFrom(const EffectChain& src)
{
    bool same = items.size() == src.items.size();
    for (size_t i = 0; same && i < items.size(); ++i)
        same = items[i].typeId == src.items[i].typeId;
    if (!same) {
        *this = src;
        return;
    }
    for (size_t i = 0; i < items.size(); ++i)
        items[i].updateFrom(src.items[i]);
}

media::Json EffectChain::toJson() const
{
    media::Json arr = media::Json::array();
    for (const EffectInstance& e : items)
        arr.push_back(e.toJson());
    return arr;
}

EffectChain EffectChain::fromJson(const media::Json& j, std::vector<std::string>* warnings)
{
    EffectChain chain;
    if (!j.is_array())
        return chain;
    for (const media::Json& item : j) {
        if (chain.items.size() >= kMaxEffects) {
            if (warnings)
                warnings->push_back("Cadeia de efeitos cortada em " + std::to_string(kMaxEffects) + " efeitos.");
            break;
        }
        EffectInstance e = EffectInstance::fromJson(item);
        if (e.typeId.empty()) {
            if (warnings)
                warnings->push_back("Efeito sem tipo ignorado.");
            continue;
        }
        chain.items.push_back(std::move(e));
    }
    return chain;
}

}
