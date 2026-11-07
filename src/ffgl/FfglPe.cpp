#include "ffgl/FfglPe.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace bsl::ffgl {

namespace {

class FileReader
{
public:
    explicit FileReader(const std::filesystem::path& p) : in_(p, std::ios::binary)
    {
        if (in_) {
            in_.seekg(0, std::ios::end);
            size_ = static_cast<std::uint64_t>(in_.tellg());
        }
    }
    bool ok() const { return static_cast<bool>(in_); }
    std::uint64_t size() const { return size_; }

    bool read(std::uint64_t offset, void* dst, size_t n)
    {
        if (!in_ || offset > size_ || n > size_ - offset)
            return false;
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        return static_cast<size_t>(in_.gcount()) == n;
    }

private:
    std::ifstream in_;
    std::uint64_t size_ = 0;
};

struct Section
{
    std::uint32_t rva = 0, rawSize = 0, rawPtr = 0, virtSize = 0;
};

std::uint64_t rvaToOffset(const std::vector<Section>& sections, std::uint32_t rva)
{
    for (const Section& s : sections) {
        const std::uint32_t span = std::max(s.virtSize, s.rawSize);
        if (rva >= s.rva && rva - s.rva < span) {
            const std::uint32_t inSection = rva - s.rva;
            if (inSection >= s.rawSize)
                return 0;
            return static_cast<std::uint64_t>(s.rawPtr) + inSection;
        }
    }
    return 0;
}

}

PeVerdict inspectPe(const std::filesystem::path& file, std::string* detail)
{
    auto fail = [&](PeVerdict v, const char* why) {
        if (detail)
            *detail = why;
        return v;
    };

    FileReader r(file);
    if (!r.ok())
        return fail(PeVerdict::Unreadable, "não foi possível abrir o arquivo");

    IMAGE_DOS_HEADER dos{};
    if (!r.read(0, &dos, sizeof(dos)))
        return fail(PeVerdict::NotPe, "arquivo pequeno demais");
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
        return fail(PeVerdict::NotPe, "sem cabeçalho MZ");

    const std::uint64_t ntOff = static_cast<std::uint64_t>(dos.e_lfanew);
    std::uint32_t sig = 0;
    IMAGE_FILE_HEADER fh{};
    if (!r.read(ntOff, &sig, sizeof(sig)) || sig != IMAGE_NT_SIGNATURE || !r.read(ntOff + 4, &fh, sizeof(fh)))
        return fail(PeVerdict::NotPe, "sem assinatura PE");
    if (fh.Machine != IMAGE_FILE_MACHINE_AMD64)
        return fail(PeVerdict::NotX64, "não é x64");
    if ((fh.Characteristics & IMAGE_FILE_DLL) == 0)
        return fail(PeVerdict::NoPlugMain, "não é uma DLL");

    IMAGE_OPTIONAL_HEADER64 opt{};
    const size_t optSize = std::min<size_t>(fh.SizeOfOptionalHeader, sizeof(opt));
    if (optSize < offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + sizeof(IMAGE_DATA_DIRECTORY)
        || !r.read(ntOff + 4 + sizeof(fh), &opt, optSize))
        return fail(PeVerdict::NotPe, "cabeçalho opcional inválido");
    if (opt.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return fail(PeVerdict::NotX64, "não é PE32+");
    if (opt.NumberOfRvaAndSizes == 0)
        return fail(PeVerdict::NoPlugMain, "sem diretórios de dados");

    const IMAGE_DATA_DIRECTORY exportDir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (exportDir.VirtualAddress == 0 || exportDir.Size == 0)
        return fail(PeVerdict::NoPlugMain, "a DLL não exporta nada");

    std::vector<Section> sections;
    const std::uint64_t secOff = ntOff + 4 + sizeof(fh) + fh.SizeOfOptionalHeader;
    const unsigned nSections = std::min<unsigned>(fh.NumberOfSections, 96);
    for (unsigned i = 0; i < nSections; ++i) {
        IMAGE_SECTION_HEADER sh{};
        if (!r.read(secOff + static_cast<std::uint64_t>(i) * sizeof(sh), &sh, sizeof(sh)))
            return fail(PeVerdict::NotPe, "tabela de seções truncada");
        sections.push_back({sh.VirtualAddress, sh.SizeOfRawData, sh.PointerToRawData, sh.Misc.VirtualSize});
    }

    const std::uint64_t expOff = rvaToOffset(sections, exportDir.VirtualAddress);
    IMAGE_EXPORT_DIRECTORY ed{};
    if (expOff == 0 || !r.read(expOff, &ed, sizeof(ed)))
        return fail(PeVerdict::NotPe, "tabela de exportação ilegível");
    if (ed.NumberOfNames == 0 || ed.NumberOfNames > 65535)
        return fail(PeVerdict::NoPlugMain, "a DLL não exporta funções com nome");

    const std::uint64_t namesOff = rvaToOffset(sections, ed.AddressOfNames);
    if (namesOff == 0)
        return fail(PeVerdict::NotPe, "tabela de nomes ilegível");
    std::vector<std::uint32_t> nameRvas(ed.NumberOfNames);
    if (!r.read(namesOff, nameRvas.data(), nameRvas.size() * sizeof(std::uint32_t)))
        return fail(PeVerdict::NotPe, "tabela de nomes truncada");

    static const char kWanted[] = "plugMain";
    for (const std::uint32_t nameRva : nameRvas) {
        const std::uint64_t off = rvaToOffset(sections, nameRva);
        char buf[sizeof(kWanted) + 1] = {};
        if (off == 0 || !r.read(off, buf, std::min<std::uint64_t>(sizeof(buf), r.size() - off)))
            continue;
        if (std::memcmp(buf, kWanted, sizeof(kWanted)) == 0)
            return PeVerdict::Candidate;
    }
    return fail(PeVerdict::NoPlugMain, "não exporta plugMain");
}

const char* peVerdictText(PeVerdict v)
{
    switch (v) {
    case PeVerdict::Candidate: return "candidata a plugin FFGL";
    case PeVerdict::Unreadable: return "ilegível";
    case PeVerdict::NotPe: return "não é um executável PE";
    case PeVerdict::NotX64: return "não é x64";
    case PeVerdict::NoPlugMain: return "não exporta plugMain";
    }
    return "";
}

}
