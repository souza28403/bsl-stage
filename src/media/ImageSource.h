#pragma once

#include "media/ISource.h"
#include "media/ImageDecoder.h"

#include <vector>

namespace bsl::media {

// Imagem estatica ou animada (GIF/WEBP) como textura RGBA. Imagens estaticas liberam a copia em CPU
// depois do upload. Em animadas, duration() e a soma dos delays e os modos Loop/Once/PingPong valem.
class ImageSource final : public ISource {
public:
    ImageSource() = default;
    ~ImageSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override { return tex_; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return hasAlpha_; }

    bool isAnimated() const { return frames_.size() > 1; }
    size_t frameCount() const { return frames_.empty() ? (open_ ? 1 : 0) : frames_.size(); }

protected:
    void onSeek(double seconds) override;

private:
    size_t frameIndexAt(double t) const;
    void showFrame(size_t index);

    bool open_ = false;
    GLuint tex_ = 0;
    int texW_ = 0;
    int texH_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool hasAlpha_ = false;
    std::vector<Frame> frames_;      // vazio para imagem estatica (ja enviada a GPU)
    std::vector<double> starts_;     // inicio de cada frame (s)
    size_t current_ = static_cast<size_t>(-1);
};

} // namespace bsl::media
