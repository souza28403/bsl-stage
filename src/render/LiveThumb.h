#pragma once

#include "render/Fbo.h"
#include "render/Shader.h"
#include "render/Texture.h"

#include <memory>

namespace bsl {

class LiveThumbnailer
{
public:
    static constexpr int kWidth = 160;
    static constexpr int kHeight = 90;

    LiveThumbnailer();
    ~LiveThumbnailer();
    LiveThumbnailer(const LiveThumbnailer&) = delete;
    LiveThumbnailer& operator=(const LiveThumbnailer&) = delete;

    void blit(GLuint srcTexture, int srcWidth, int srcHeight, Texture& dst);

private:
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    GLuint fbo_ = 0;
};

}
