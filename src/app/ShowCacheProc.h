#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace bsl::detail {

class ShowProc
{
public:
    ~ShowProc();
    ShowProc(const ShowProc&) = delete;
    ShowProc& operator=(const ShowProc&) = delete;

    static std::unique_ptr<ShowProc> start(const std::filesystem::path& job, const std::filesystem::path& progress,
                                           std::string& error);
    bool running(int& exitCode);
    void kill();

private:
    ShowProc() = default;
    void* process_ = nullptr;
};

}
