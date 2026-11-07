#pragma once

#include "core/Composition.h"
#include "fx/FxGl.h"
#include "output/OutputSetup.h"
#include "render/Fbo.h"
#include "render/FboPool.h"
#include "render/Shader.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

namespace bsl {

class Compositor
{
public:
    Compositor(int width, int height);
    ~Compositor();
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    void resize(int width, int height);

    void render(Composition& comp, float previewScale, bool eco = false,
                const std::vector<LayerViewRequest>& layerViews = {});

    GLuint texture() const { return fbo_[current_].texture(); }
    int width() const { return fbo_[0].width(); }
    int height() const { return fbo_[0].height(); }

    float uvMax() const { return renderedScale_; }

    bool layerView(int layerId, bool includeMaster, GLuint& texture, bool applyTransform = false) const;
    int layerViewCount() const { return static_cast<int>(views_.size()); }

    FboPool& pool() { return pool_; }

private:
    struct View
    {
        int layerId = 0;
        bool includeMaster = false;
        bool applyTransform = false;
        int layerIndex = -1;
        FboPool::Lease lease;
    };

    Fbo fbo_[2];
    int current_ = 0;
    Shader shader_;
    Shader fit_;
    Shader finish_;
    GLuint vao_ = 0;
    float renderedScale_ = 1.0f;

    FboPool pool_;
    fx::TexBlitter blit_;
    std::chrono::steady_clock::time_point lastRender_{};
    bool haveLast_ = false;
    double time_ = 0.0;
    std::uint64_t frame_ = 0;

    std::vector<View> views_;
    std::map<int, fx::EffectChain> masterCopies_;
};

}
