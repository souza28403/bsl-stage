#include "worker/PdfDocument.h"

#include <fpdfview.h>
#include <fpdf_doc.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bsl::worker {

namespace {

void ensureLibrary()
{
    static std::once_flag once;
    std::call_once(once, [] {
        FPDF_InitLibrary();
    });
}

const char* errorText(unsigned long code)
{
    switch (code) {
    case FPDF_ERR_FILE: return "não foi possível ler o arquivo PDF";
    case FPDF_ERR_FORMAT: return "o arquivo não é um PDF válido";
    case FPDF_ERR_PASSWORD: return "o PDF está protegido por senha";
    case FPDF_ERR_SECURITY: return "o PDF usa uma proteção não suportada";
    case FPDF_ERR_PAGE: return "página inválida no PDF";
    default: return "erro ao abrir o PDF";
    }
}

std::string utf16ToUtf8(const char16_t* s)
{
    std::string out;
    for (; *s; ++s) {
        char32_t cp = *s;
        if (cp >= 0xD800 && cp < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<char32_t>(s[1]) - 0xDC00);
            ++s;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}

struct PdfDocument::FileAccess
{
    FPDF_FILEACCESS access{};
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
#else
    int file = -1;
#endif
};

PdfDocument::PdfDocument()
{
    ensureLibrary();
}

PdfDocument::~PdfDocument()
{
    close();
}

void PdfDocument::close()
{
    if (doc_) {
        FPDF_CloseDocument(static_cast<FPDF_DOCUMENT>(doc_));
        doc_ = nullptr;
    }
    if (file_) {
#ifdef _WIN32
        if (file_->file != INVALID_HANDLE_VALUE)
            CloseHandle(file_->file);
#else
        if (file_->file >= 0)
            ::close(file_->file);
#endif
        delete file_;
        file_ = nullptr;
    }
    pages_ = 0;
}

bool PdfDocument::open(const std::filesystem::path& path, std::string* error)
{
    close();
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (error)
            *error = "arquivo não encontrado ou sem permissão de leitura";
        return false;
    }
    LARGE_INTEGER sizeInfo{};
    GetFileSizeEx(h, &sizeInfo);
    const long long size = sizeInfo.QuadPart;
#else
    const int h = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (h < 0) {
        if (error)
            *error = "arquivo não encontrado ou sem permissão de leitura";
        return false;
    }
    struct stat st{};
    const long long size = ::fstat(h, &st) == 0 ? static_cast<long long>(st.st_size) : 0;
#endif
    if (size <= 0 || size > 0x7FFFFFFF) {
#ifdef _WIN32
        CloseHandle(h);
#else
        ::close(h);
#endif
        if (error)
            *error = size <= 0 ? "o arquivo está vazio" : "o PDF é grande demais";
        return false;
    }
    file_ = new FileAccess();
    file_->file = h;
    file_->access.m_FileLen = static_cast<unsigned long>(size);
    file_->access.m_Param = file_;
    file_->access.m_GetBlock = [](void* param, unsigned long position, unsigned char* buf, unsigned long count) -> int {
        auto* fa = static_cast<FileAccess*>(param);
#ifdef _WIN32
        OVERLAPPED ov{};
        ov.Offset = position;
        DWORD got = 0;
        if (!ReadFile(fa->file, buf, count, &got, &ov) || got != count)
            return 0;
        return 1;
#else
        unsigned long done = 0;
        while (done < count) {
            const ssize_t n = ::pread(fa->file, buf + done, count - done, static_cast<off_t>(position + done));
            if (n <= 0)
                return 0;
            done += static_cast<unsigned long>(n);
        }
        return 1;
#endif
    };

    FPDF_DOCUMENT doc = FPDF_LoadCustomDocument(&file_->access, nullptr);
    if (!doc) {
        const unsigned long err = FPDF_GetLastError();
        close();
        if (error)
            *error = errorText(err);
        return false;
    }
    doc_ = doc;
    pages_ = FPDF_GetPageCount(doc);
    if (pages_ <= 0) {
        close();
        if (error)
            *error = "o PDF não tem páginas";
        return false;
    }
    return true;
}

std::string PdfDocument::title() const
{
    if (!doc_)
        return {};
    const unsigned long bytes = FPDF_GetMetaText(static_cast<FPDF_DOCUMENT>(doc_), "Title", nullptr, 0);
    if (bytes <= 2)
        return {};
    std::vector<char16_t> buf(bytes / 2 + 1, u'\0');
    FPDF_GetMetaText(static_cast<FPDF_DOCUMENT>(doc_), "Title", buf.data(), bytes);
    return utf16ToUtf8(buf.data());
}

bool PdfDocument::render(int page, int w, int h, const std::uint8_t bg[4], std::vector<std::uint8_t>& out)
{
    if (!doc_ || w <= 0 || h <= 0)
        return false;
    page = std::clamp(page, 1, pages_);
    FPDF_PAGE p = FPDF_LoadPage(static_cast<FPDF_DOCUMENT>(doc_), page - 1);
    if (!p)
        return false;
    const double pw = std::max(1.0, static_cast<double>(FPDF_GetPageWidthF(p)));
    const double ph = std::max(1.0, static_cast<double>(FPDF_GetPageHeightF(p)));
    const double scale = std::min(static_cast<double>(w) / pw, static_cast<double>(h) / ph);
    const int rw = std::clamp(static_cast<int>(std::lround(pw * scale)), 1, w);
    const int rh = std::clamp(static_cast<int>(std::lround(ph * scale)), 1, h);
    const int x0 = (w - rw) / 2;
    const int y0 = (h - rh) / 2;

    out.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u);
    for (std::size_t i = 0; i < out.size(); i += 4) {
        out[i] = bg[0];
        out[i + 1] = bg[1];
        out[i + 2] = bg[2];
        out[i + 3] = bg[3];
    }
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, out.data(), w * 4);
    if (!bmp) {
        FPDF_ClosePage(p);
        return false;
    }
    FPDFBitmap_FillRect(bmp, x0, y0, rw, rh, 0xFFFFFFFFu);
    FPDF_RenderPageBitmap(bmp, p, x0, y0, rw, rh, 0, FPDF_ANNOT);
    FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(p);
    return true;
}

}
