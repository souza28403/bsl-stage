#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::worker {

class PdfDocument
{
public:
    PdfDocument();
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    bool open(const std::filesystem::path& file, std::string* error);
    void close();
    bool isOpen() const { return doc_ != nullptr; }
    int pageCount() const { return pages_; }
    std::string title() const;

    bool render(int page, int w, int h, const std::uint8_t bgColorBgra[4], std::vector<std::uint8_t>& out);

private:
    struct FileAccess;
    void* doc_ = nullptr;
    int pages_ = 0;
    FileAccess* file_ = nullptr;
};

}
