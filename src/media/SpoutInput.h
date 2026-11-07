#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bsl {

class SpoutInput
{
public:
    SpoutInput();
    ~SpoutInput();
    SpoutInput(const SpoutInput&) = delete;
    SpoutInput& operator=(const SpoutInput&) = delete;

    void setSender(const std::string& name);
    const std::string& sender() const { return sender_; }

    bool receive();

    unsigned int texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool connected() const { return connected_; }
    unsigned long long frames() const { return frames_; }
    const std::string& connectedName() const { return connectedName_; }

    void release();

    static std::vector<std::string> listSenders();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string sender_;
    std::string connectedName_;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool connected_ = false;
    unsigned long long frames_ = 0;
};

}
