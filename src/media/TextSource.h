#pragma once

#include "media/ISource.h"
#include "media/TextRaster.h"

#include <memory>

namespace bsl {
class Fbo;
class Shader;
}

namespace bsl::media {

class TextSource final : public ISource
{
public:
    TextSource(int width, int height);
    ~TextSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool hasAlpha() const override { return true; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    bool scrolling() const { return period_ > 0; }
    double scrollOffset() const { return scroll_; }
    int stripWidth() const { return stripW_; }
    int stripHeight() const { return stripH_; }
    int rasterCount() const { return rasterCount_; }

protected:
    void onSeek(double seconds) override;
    void onParamsChanged() override { dirty_ = true; }

private:
    void rebuild();
    void render();
    void resetScroll();

    int width_;
    int height_;
    bool open_ = false;
    bool dirty_ = true;

    std::unique_ptr<Fbo> fbo_;
    std::unique_ptr<Shader> shader_;
    GLuint vao_ = 0;
    GLuint strip_ = 0;
    int stripW_ = 0;
    int stripH_ = 0;
    int period_ = 0;
    TextMarquee marquee_ = TextMarquee::None;
    float speed_ = 0.0f;
    double scroll_ = 0.0;
    bool needsDraw_ = true;
    int rasterCount_ = 0;

    bool fontFallback_ = false;
    bool fontMissing_ = false;
    bool truncated_ = false;
    std::string fontName_;
};

}
