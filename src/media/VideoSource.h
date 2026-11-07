#pragma once

#include "media/ISource.h"
#include "media/VideoDecoder.h"
#include "media/YuvConverter.h"

#include <memory>

namespace bsl::media {

class VideoSource final : public ISource {
public:
    explicit VideoSource(VideoDecoderOptions options = {});
    ~VideoSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return decoder_.isOpen(); }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return decoder_.isOpen() && decoder_.info().hasAlpha; }
    bool supportsReverse() const override { return false; }

    const VideoInfo& info() const { return decoder_.info(); }

protected:
    void onSeek(double seconds) override;

private:
    void present(const Frame& frame);

    VideoDecoderOptions options_;
    VideoDecoder decoder_;
    std::unique_ptr<YuvConverter> yuv_;
    GLuint rgbaTex_ = 0;
    int rgbaW_ = 0;
    int rgbaH_ = 0;
    bool usingYuv_ = false;
    int width_ = 0;
    int height_ = 0;
};

}
