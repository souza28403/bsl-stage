#pragma once

#include "render/Fbo.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <memory>

namespace bsl::media {

class FadeBlender
{
public:
    FadeBlender();
    ~FadeBlender();
    FadeBlender(const FadeBlender&) = delete;
    FadeBlender& operator=(const FadeBlender&) = delete;

    void snapshot(GLuint src, int w, int h);
    void begin(GLuint current, double duration);
    bool update(double dt, GLuint current);
    bool active() const { return active_; }
    GLuint output() const { return out_ ? out_->texture() : 0; }

private:
    void draw(GLuint current, float t);
    void ensure(int w, int h);

    std::unique_ptr<Shader> mix_;
    std::unique_ptr<Shader> copy_;
    std::unique_ptr<Fbo> prev_;
    std::unique_ptr<Fbo> out_;
    GLuint vao_ = 0;
    bool active_ = false;
    double elapsed_ = 0.0;
    double duration_ = 0.5;
};

}
