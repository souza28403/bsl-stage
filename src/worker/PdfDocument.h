#pragma once

// PDF via PDFium (binarios bblanchon/pdfium-binaries, BSD). So o worker carrega a DLL.

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

    // Erro em portugues para a celula (senha, arquivo invalido...)
    bool open(const std::filesystem::path& file, std::string* error);
    void close();
    bool isOpen() const { return doc_ != nullptr; }
    int pageCount() const { return pages_; }
    std::string title() const;

    // Renderiza a pagina (1..N) num canvas w x h BGRA8: fundo 'bgColorBgra', pagina branca centralizada e
    // encaixada sem distorcer. Alpha reta. 'out' recebe w*h*4 bytes.
    bool render(int page, int w, int h, const std::uint8_t bgColorBgra[4], std::vector<std::uint8_t>& out);

private:
    struct FileAccess;
    void* doc_ = nullptr;
    int pages_ = 0;
    FileAccess* file_ = nullptr;
};

} // namespace bsl::worker
