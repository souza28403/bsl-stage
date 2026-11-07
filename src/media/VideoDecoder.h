#pragma once

#include "media/Frame.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bsl::media {

struct VideoInfo {
    std::string codec;
    std::string decoderName;
    std::string pixelFormat;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;
    bool hasAlpha = false;
};

struct VideoDecoderOptions {
    bool lowPower = false;
    int targetWidth = 0;
    int targetHeight = 0;
    int queueSize = 4;

    bool live = false;
    std::string inputFormat;
    std::vector<std::pair<std::string, std::string>> inputOptions;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const std::filesystem::path& path, const VideoDecoderOptions& options = {});
    void close();
    bool isOpen() const;

    const VideoInfo& info() const;
    const std::string& error() const;

    void seek(double seconds);

    void setLoop(bool loop);

    std::unique_ptr<Frame> tryGetFrame(double clipTime);

    std::unique_ptr<Frame> tryGetLatest();

    std::unique_ptr<Frame> nextFrame(int timeoutMs);

    bool atEnd() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
