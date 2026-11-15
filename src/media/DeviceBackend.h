#pragma once

#include "media/DeviceCatalog.h"

#include <string>
#include <utility>
#include <vector>

namespace bsl::media::devbackend {

struct OpenSpec
{
    std::string inputFormat;
    std::string url;
    std::vector<std::pair<std::string, std::string>> options;
};

std::vector<std::string> listVideoDevices();
std::vector<CaptureFormat> listVideoFormats(const std::string& device);
OpenSpec makeOpenSpec(const std::string& device, const ChosenFormat& chosen);

}
