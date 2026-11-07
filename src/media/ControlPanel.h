#pragma once

#include <string>

namespace bsl::media {

class IControlPanel
{
public:
    virtual ~IControlPanel() = default;

    virtual bool openControlServer(std::string& url, std::string* error = nullptr) = 0;
    virtual std::string controlUrl() const = 0;
    virtual bool controlExposedToNetwork() const = 0;
};

bool openUrlInBrowser(const std::string& url, std::string* error = nullptr);
bool openUrlInOwnWindow(const std::string& url, const std::string& title, std::string* error = nullptr);
void closeOwnControlWindow();
unsigned long ownControlWindowPid();

}
