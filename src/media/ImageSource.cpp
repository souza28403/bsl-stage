#include "media/ImageSource.h"

#include "media/YuvConverter.h"

#include <algorithm>

namespace bsl::media {

ImageSource::~ImageSource()
{
    close();
}

bool ImageSource::open(const std::filesystem::path& path, std::string* error)
{
    close();
    DecodedImage img;
    if (!decodeImage(path, img, error))
        return false;

    width_ = img.width;
    height_ = img.height;
    hasAlpha_ = img.hasAlpha;
    open_ = true;

    const LoopMode keepMode = clock_.loop;
    const double keepSpeed = clock_.speed;
    clock_ = ClipClock{};
    clock_.loop = keepMode;
    clock_.speed = keepSpeed;

    if (img.animated()) {
        starts_.clear();
        double acc = 0.0;
        for (double d : img.delays) {
            starts_.push_back(acc);
            acc += d;
        }
        clock_.duration = acc;
        frames_ = std::move(img.frames);
        showFrame(0);
    } else {
        uploadRgbaTexture(tex_, texW_, texH_, img.frames.front());
        current_ = 0;
    }
    return true;
}

void ImageSource::close()
{
    if (tex_) {
        glDeleteTextures(1, &tex_);
        tex_ = 0;
    }
    texW_ = texH_ = width_ = height_ = 0;
    frames_.clear();
    starts_.clear();
    current_ = static_cast<size_t>(-1);
    hasAlpha_ = false;
    open_ = false;
    clock_.duration = 0.0;
    clock_.position = 0.0;
}

size_t ImageSource::frameIndexAt(double t) const
{
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), t);
    return it == starts_.begin() ? 0 : static_cast<size_t>(it - starts_.begin()) - 1;
}

void ImageSource::showFrame(size_t index)
{
    if (index >= frames_.size() || index == current_)
        return;
    uploadRgbaTexture(tex_, texW_, texH_, frames_[index]);
    current_ = index;
}

void ImageSource::onSeek(double)
{
    if (isAnimated())
        showFrame(frameIndexAt(clock_.position));
}

void ImageSource::update(double dt)
{
    if (!open_ || !isAnimated())
        return;
    clock_.advance(dt);
    showFrame(frameIndexAt(clock_.position));
}

} // namespace bsl::media
