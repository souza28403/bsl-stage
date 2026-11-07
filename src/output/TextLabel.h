#pragma once

#include <string>
#include <vector>

namespace bsl {

struct LabelBitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    bool empty() const { return width <= 0 || height <= 0; }
};

LabelBitmap renderLabel(const std::string& line1, float pixelHeight1, const std::string& line2, float pixelHeight2);

}
