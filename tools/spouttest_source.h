#pragma once

#include <functional>
#include <string>
#include <vector>

namespace spouttest {

struct AppSourceResult
{
    bool registered = false;
    bool isLive = false;
    bool listed = false;
    bool connected = false;
    int width = 0;
    int height = 0;
    std::string status;
    std::vector<unsigned char> rgba;
};

bool receiveWithAppSource(const std::string& sender, const std::function<void()>& sendOne, AppSourceResult& out);

}
