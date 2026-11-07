#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace bsl::update {

class Sha256
{
public:
    Sha256();
    void update(const void* data, size_t size);
    std::array<uint8_t, 32> finish();
    static std::string toHex(const std::array<uint8_t, 32>& digest);

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};

std::string sha256Hex(std::string_view data);

bool sha256File(const std::filesystem::path& file, std::string& hexOut,
                const std::function<bool(uint64_t done)>& progress = {});

}
