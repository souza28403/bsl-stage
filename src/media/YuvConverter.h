#pragma once

#include "media/Frame.h"

#include <glad/gl.h>

#include <memory>

namespace bsl {
class Fbo;
class Shader;
}

namespace bsl::media {

void uploadRgbaTexture(GLuint& tex, int& texW, int& texH, const Frame& frame);

class YuvConverter {
public:
    YuvConverter();
    ~YuvConverter();
    YuvConverter(const YuvConverter&) = delete;
    YuvConverter& operator=(const YuvConverter&) = delete;

    void convert(const Frame& frame);

    GLuint texture() const;
    int width() const;
    int height() const;

private:
    void uploadPlane(int index, const Frame& frame);

    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Fbo> fbo_;
    GLuint vao_ = 0;
    GLuint planeTex_[4] = {0, 0, 0, 0};
    int planeW_[4] = {0, 0, 0, 0};
    int planeH_[4] = {0, 0, 0, 0};
};

class DxtConverter {
public:
    DxtConverter();
    ~DxtConverter();
    DxtConverter(const DxtConverter&) = delete;
    DxtConverter& operator=(const DxtConverter&) = delete;

    static bool supported();

    void upload(const Frame& frame);

    GLuint texture() const;
    int width() const { return width_; }
    int height() const { return height_; }

private:
    std::unique_ptr<Shader> shader_;
    std::unique_ptr<Fbo> fbo_;
    GLuint vao_ = 0;
    GLuint tex_ = 0;
    static constexpr int kPboCount = 3;
    GLuint pbo_[kPboCount] = {0, 0, 0};
    int pboIndex_ = 0;
    GLsizei pboBytes_ = 0;
    GLenum texFormat_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool ycocg_ = false;
};

}
