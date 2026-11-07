#pragma once

#include "output/OutputSetup.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

struct FeedSlice
{
    std::string name;
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    std::string id;
    std::string screen;
    std::string sliceName;
    float cx = 0.0f, cy = 0.0f, hw = 0.0f, hh = 0.0f, angle = 0.0f;

    bool operator==(const FeedSlice& o) const
    {
        return name == o.name && x == o.x && y == o.y && w == o.w && h == o.h && id == o.id && cx == o.cx && cy == o.cy &&
               hw == o.hw && hh == o.hh && angle == o.angle;
    }
};

class SliceFeed
{
public:
    static SliceFeed& instance();

    void set(std::vector<FeedSlice> slices, int canvasW, int canvasH);
    std::vector<FeedSlice> slices() const;
    int canvasWidth() const;
    int canvasHeight() const;
    std::uint64_t version() const;

private:
    mutable std::mutex mutex_;
    std::vector<FeedSlice> slices_;
    int canvasW_ = 1920;
    int canvasH_ = 1080;
    std::uint64_t version_ = 0;
};

std::vector<FeedSlice> sliceFeedFromOutput(const OutputSetup& output);

std::vector<FeedSlice> parseSliceXml(const std::string& xml, int* compW = nullptr, int* compH = nullptr);

void orientedFromVerts(const float vx[4], const float vy[4], float& cx, float& cy, float& hw, float& hh, float& angleDeg);

}
