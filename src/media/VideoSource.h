#pragma once

#include "media/ISource.h"
#include "media/VideoDecoder.h"
#include "media/YuvConverter.h"

#include <memory>

namespace bsl::media {

// Video (qualquer formato do FFmpeg) como textura RGBA. Decodifica em thread propria;
// frames YUV viram RGBA via shader (YuvConverter), os demais ja chegam em RGBA.
// Limitacao: so toca para frente (supportsReverse() == false): ping-pong vira Loop e speed < 0 vira 0.
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
    GLuint rgbaTex_ = 0; // caminho RGBA e placeholder 1x1 antes do primeiro frame
    int rgbaW_ = 0;
    int rgbaH_ = 0;
    bool usingYuv_ = false;
    int width_ = 0;
    int height_ = 0;
};

} // namespace bsl::media
