#include "media/VideoSource.h"

namespace bsl::media {

VideoSource::VideoSource(VideoDecoderOptions options) : options_(options) {}

VideoSource::~VideoSource()
{
    close();
}

bool VideoSource::open(const std::filesystem::path& path, std::string* error)
{
    close();
    if (!decoder_.open(path, options_)) {
        if (error)
            *error = decoder_.error();
        return false;
    }
    const LoopMode keepMode = clock_.loop;
    const double keepSpeed = clock_.speed;
    clock_ = ClipClock{};
    clock_.loop = keepMode;
    clock_.speed = keepSpeed;
    clock_.duration = decoder_.info().duration;
    width_ = decoder_.info().width;
    height_ = decoder_.info().height;

    // Placeholder transparente 1x1 ate chegar o primeiro frame.
    Frame blank;
    blank.allocate(1, 1, PixelFormat::RGBA8);
    uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, blank);
    usingYuv_ = false;
    decoder_.setLoop(clock_.loop != LoopMode::Once);
    return true;
}

void VideoSource::close()
{
    decoder_.close();
    yuv_.reset();
    if (rgbaTex_) {
        glDeleteTextures(1, &rgbaTex_);
        rgbaTex_ = 0;
    }
    rgbaW_ = rgbaH_ = 0;
    usingYuv_ = false;
    width_ = height_ = 0;
    clock_.duration = 0.0;
    clock_.position = 0.0;
}

GLuint VideoSource::texture() const
{
    return (usingYuv_ && yuv_) ? yuv_->texture() : rgbaTex_;
}

void VideoSource::onSeek(double seconds)
{
    decoder_.seek(seconds);
}

void VideoSource::update(double dt)
{
    if (!decoder_.isOpen())
        return;
    decoder_.setLoop(clock_.loop != LoopMode::Once);
    clock_.advance(dt);
    if (auto f = decoder_.tryGetFrame(clock_.position))
        present(*f);
}

void VideoSource::present(const Frame& frame)
{
    if (frame.isYuv()) {
        if (!yuv_)
            yuv_ = std::make_unique<YuvConverter>();
        yuv_->convert(frame);
        usingYuv_ = true;
    } else {
        uploadRgbaTexture(rgbaTex_, rgbaW_, rgbaH_, frame);
        usingYuv_ = false;
    }
    width_ = frame.width;
    height_ = frame.height;
}

} // namespace bsl::media
