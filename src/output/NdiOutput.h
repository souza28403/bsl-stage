#pragma once

#include <memory>
#include <string>

namespace bsl::ndi {
class Runtime;
}

namespace bsl {

class NdiOutput
{
public:
    explicit NdiOutput(ndi::Runtime* runtime = nullptr);
    ~NdiOutput();
    NdiOutput(const NdiOutput&) = delete;
    NdiOutput& operator=(const NdiOutput&) = delete;

    void setName(const std::string& name);
    const std::string& name() const { return name_; }
    void setBgra(bool bgra);
    bool bgra() const { return bgra_; }
    void setFrameRate(float fps);

    bool send(unsigned int texture, int width, int height);

    void release();

    bool runtimeAvailable() const;
    int connections() const { return connections_; }
    unsigned long long framesSent() const { return framesSent_; }
    std::string actualName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ndi::Runtime* runtime_ = nullptr;
    std::string name_ = "BSL Stage";
    bool bgra_ = false;
    float fps_ = 30.0f;
    int connections_ = 0;
    unsigned long long framesSent_ = 0;
};

}
