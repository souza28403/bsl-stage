#include "fx/bsl/SliceFeed.h"

#include "app/Paths.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <windows.h>
#include <shlobj.h>

namespace bsl::fx::bsllight {

SliceFeed& SliceFeed::instance()
{
    static SliceFeed feed;
    return feed;
}

void SliceFeed::set(std::vector<FeedSlice> slices, int canvasW, int canvasH)
{
    std::lock_guard lk(mutex_);
    if (slices == slices_ && canvasW == canvasW_ && canvasH == canvasH_)
        return;
    slices_ = std::move(slices);
    canvasW_ = canvasW;
    canvasH_ = canvasH;
    ++version_;
}

std::vector<FeedSlice> SliceFeed::slices() const
{
    std::lock_guard lk(mutex_);
    return slices_;
}

int SliceFeed::canvasWidth() const
{
    std::lock_guard lk(mutex_);
    return canvasW_;
}

int SliceFeed::canvasHeight() const
{
    std::lock_guard lk(mutex_);
    return canvasH_;
}

std::uint64_t SliceFeed::version() const
{
    std::lock_guard lk(mutex_);
    return version_;
}

void orientedFromVerts(const float vx[4], const float vy[4], float& cx, float& cy, float& hw, float& hh, float& angleDeg)
{
    double x[4], y[4];
    double mx = 0, my = 0;
    for (int i = 0; i < 4; ++i) {
        x[i] = vx[i];
        y[i] = vy[i];
        mx += x[i];
        my += y[i];
    }
    mx *= 0.25;
    my *= 0.25;
    const double e1x = x[1] - x[0], e1y = y[1] - y[0];
    const double e2x = x[3] - x[0], e2y = y[3] - y[0];
    double w = std::sqrt(e1x * e1x + e1y * e1y);
    double h = std::sqrt(e2x * e2x + e2y * e2y);
    double ang = std::atan2(e1y, e1x) * 180.0 / 3.14159265358979323846;
    for (int k = 0; k < 4 && ang > 45.0; ++k) {
        ang -= 90.0;
        std::swap(w, h);
    }
    for (int k = 0; k < 4 && ang <= -45.0; ++k) {
        ang += 90.0;
        std::swap(w, h);
    }
    if (std::fabs(ang) > 135.0)
        ang = 0.0;
    cx = static_cast<float>(mx);
    cy = static_cast<float>(my);
    hw = static_cast<float>(w * 0.5);
    hh = static_cast<float>(h * 0.5);
    angleDeg = static_cast<float>(ang);
}

std::vector<FeedSlice> sliceFeedFromOutput(const OutputSetup& output)
{
    std::vector<FeedSlice> out;
    for (const Screen& screen : output.screens) {
        int index = 0;
        for (const Slice& s : screen.slices) {
            const int at = index++;
            if (!s.enabled)
                continue;
            FeedSlice f;
            f.screen = screen.name;
            f.sliceName = s.name;
            f.name = screen.name + " / " + s.name;
            f.id = screen.name + "/" + s.name + "#" + std::to_string(at);
            f.x = static_cast<float>(s.input.x);
            f.y = static_cast<float>(s.input.y);
            f.w = static_cast<float>(s.input.w);
            f.h = static_cast<float>(s.input.h);
            f.cx = f.x + f.w * 0.5f;
            f.cy = f.y + f.h * 0.5f;
            f.hw = f.w * 0.5f;
            f.hh = f.h * 0.5f;
            f.angle = 0.0f;
            out.push_back(std::move(f));
        }
    }
    return out;
}

namespace {

bool parseNumber(std::string s, float& out)
{
    std::replace(s.begin(), s.end(), ',', '.');
    double v = 0.0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc())
        return false;
    out = static_cast<float>(v);
    return true;
}

bool attribute(const std::string& text, size_t pos, size_t limit, const char* name, std::string& out)
{
    const std::string key = std::string(name) + "=\"";
    const size_t at = text.find(key, pos);
    if (at == std::string::npos || at >= limit)
        return false;
    const size_t begin = at + key.size();
    const size_t end = text.find('"', begin);
    if (end == std::string::npos || end > limit)
        return false;
    out = text.substr(begin, end - begin);
    return true;
}

std::string decodeEntities(std::string s)
{
    struct Pair
    {
        const char* from;
        const char* to;
    };
    for (const Pair& p : {Pair{"&lt;", "<"}, Pair{"&gt;", ">"}, Pair{"&quot;", "\""}, Pair{"&apos;", "'"}, Pair{"&amp;", "&"}}) {
        size_t at = 0;
        const std::string from = p.from;
        while ((at = s.find(from, at)) != std::string::npos) {
            s.replace(at, from.size(), p.to);
            ++at;
        }
    }
    return s;
}

std::string screenNameAt(const std::string& xml, size_t at)
{
    size_t pos = at;
    while (pos > 0) {
        const size_t s = xml.rfind("<Screen", pos - 1);
        if (s == std::string::npos)
            return "Screen";
        const char after = s + 7 < xml.size() ? xml[s + 7] : '\0';
        if (after != ' ' && after != '>' && after != '\t' && after != '\n' && after != '\r') {
            if (s == 0)
                break;
            pos = s;
            continue;
        }
        const size_t namePos = xml.find("<Param name=\"Name\"", s);
        std::string value;
        if (namePos != std::string::npos && namePos < at && attribute(xml, namePos, at, "value", value) && !value.empty())
            return decodeEntities(value);
        std::string tagName;
        const size_t tagEnd = xml.find('>', s);
        if (tagEnd != std::string::npos && attribute(xml, s, tagEnd, "name", tagName) && !tagName.empty())
            return decodeEntities(tagName);
        return "Screen";
    }
    return "Screen";
}

}

std::vector<FeedSlice> parseSliceXml(const std::string& xml, int* compW, int* compH)
{
    std::vector<FeedSlice> out;
    if (compW)
        *compW = 0;
    if (compH)
        *compH = 0;
    if (compW && compH) {
        const size_t sz = xml.find("<CurrentCompositionTextureSize");
        if (sz != std::string::npos) {
            const size_t end = xml.find('>', sz);
            std::string sw, sh;
            float w = 0.0f, h = 0.0f;
            if (end != std::string::npos && attribute(xml, sz, end, "width", sw) && attribute(xml, sz, end, "height", sh) &&
                parseNumber(sw, w) && parseNumber(sh, h) && w >= 16.0f && h >= 16.0f && w <= 32768.0f && h <= 32768.0f) {
                *compW = static_cast<int>(w + 0.5f);
                *compH = static_cast<int>(h + 0.5f);
            }
        }
    }

    size_t pos = 0;
    std::string lastScreen;
    int indexInScreen = 0;
    while (out.size() < 4096) {
        size_t at = xml.find("<Slice", pos);
        if (at == std::string::npos)
            break;
        const char after = at + 6 < xml.size() ? xml[at + 6] : '\0';
        if (after != ' ' && after != '>' && after != '\t' && after != '\n' && after != '\r') {
            pos = at + 6;
            continue;
        }
        const size_t close = xml.find("</Slice>", at);
        if (close == std::string::npos)
            break;
        pos = close + 8;

        const std::string screen = screenNameAt(xml, at);
        if (screen != lastScreen) {
            lastScreen = screen;
            indexInScreen = 0;
        }
        const int sliceIndex = indexInScreen++;

        FeedSlice f;
        f.name = "Slice";
        const size_t namePos = xml.find("<Param name=\"Name\"", at);
        if (namePos != std::string::npos && namePos < close) {
            std::string value;
            if (attribute(xml, namePos, close, "value", value))
                f.name = decodeEntities(value);
        }
        const size_t rectPos = xml.find("<InputRect", at);
        if (rectPos == std::string::npos || rectPos >= close)
            continue;
        const size_t rectEnd = std::min(xml.find("</InputRect>", rectPos), close);
        float minX = std::numeric_limits<float>::max(), minY = minX;
        float maxX = std::numeric_limits<float>::lowest(), maxY = maxX;
        float vx[4] = {}, vy[4] = {};
        size_t v = rectPos;
        int found = 0;
        while (found < 4) {
            v = xml.find("<v ", v);
            if (v == std::string::npos || v >= rectEnd)
                break;
            std::string sx, sy;
            float x = 0.0f, y = 0.0f;
            if (attribute(xml, v, rectEnd, "x", sx) && attribute(xml, v, rectEnd, "y", sy) && parseNumber(sx, x) &&
                parseNumber(sy, y)) {
                vx[found] = x;
                vy[found] = y;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
                ++found;
            }
            v += 3;
        }
        if (found == 0)
            continue;
        f.x = minX;
        f.y = minY;
        f.w = maxX - minX;
        f.h = maxY - minY;
        f.screen = screen;
        f.sliceName = f.name;
        f.id = screen + "/" + f.name + "#" + std::to_string(sliceIndex);
        if (found == 4) {
            orientedFromVerts(vx, vy, f.cx, f.cy, f.hw, f.hh, f.angle);
        } else {
            f.cx = f.x + f.w * 0.5f;
            f.cy = f.y + f.h * 0.5f;
            f.hw = f.w * 0.5f;
            f.hh = f.h * 0.5f;
        }
        out.push_back(std::move(f));
    }
    return out;
}

}
