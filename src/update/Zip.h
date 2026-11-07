#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace bsl::update {

struct ZipEntry
{
    std::string name;
    uint32_t crc32 = 0;
    uint64_t compressedSize = 0;
    uint64_t size = 0;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint64_t localOffset = 0;
    bool isDirectory = false;
};

bool isSafeZipPath(const std::string& name);

uint32_t crc32Update(uint32_t crc, const void* data, size_t size);

class ZipReader
{
public:
    bool open(const std::filesystem::path& zipFile, std::string* error = nullptr);
    const std::vector<ZipEntry>& entries() const { return entries_; }
    const ZipEntry* find(const std::string& name) const;

    bool extract(const ZipEntry& entry, const std::filesystem::path& dest, std::string* error = nullptr,
                 const std::function<bool(uint64_t)>& progress = {});

    bool readAll(const ZipEntry& entry, std::string& out, std::string* error = nullptr);

private:
    bool extractTo(const ZipEntry& entry, const std::function<bool(const uint8_t*, size_t)>& sink, std::string* error);

    std::filesystem::path path_;
    std::ifstream file_;
    std::vector<ZipEntry> entries_;
};

}
