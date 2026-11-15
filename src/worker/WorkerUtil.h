#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::worker {

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
std::string lowerExt(const std::filesystem::path& p);

std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path pathFromUtf8(const std::string& s);
std::string envUtf8(const char* name);

std::filesystem::path localDataDir();

void fileLog(const std::string& line);

std::vector<unsigned long> processIds(const wchar_t* exeName);

std::string fnv1aHex(const std::string& data);

bool isOfficeExtension(const std::string& ext);

class ParentWatch
{
public:
    explicit ParentWatch(unsigned long pid);
    ~ParentWatch();
    ParentWatch(const ParentWatch&) = delete;
    ParentWatch& operator=(const ParentWatch&) = delete;
    bool alive() const;

private:
    unsigned long pid_ = 0;
    void* handle_ = nullptr;
};

}
