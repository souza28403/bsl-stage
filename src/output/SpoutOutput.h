#pragma once

#include <memory>
#include <string>

namespace bsl {

class SpoutOutput
{
public:
    SpoutOutput();
    ~SpoutOutput();
    SpoutOutput(const SpoutOutput&) = delete;
    SpoutOutput& operator=(const SpoutOutput&) = delete;

    void setName(const std::string& name);
    const std::string& name() const { return name_; }

    bool send(unsigned int texture, int width, int height);

    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
};

}
