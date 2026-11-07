#include "plugin/PluginFiles.h"

#include "app/Paths.h"
#include "fx/IsfDoc.h"

#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace bsl::plugin {

namespace fs = std::filesystem;

namespace {

std::string lowerAscii(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path pathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

constexpr std::uintmax_t kMaxImageBytes = 8u << 20;

}

bool isPluginImageExtension(const std::string& e)
{
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga";
}

std::string safeFileName(const std::string& name)
{
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x20 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            continue;
        out += static_cast<char>(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    size_t lead = 0;
    while (lead < out.size() && out[lead] == ' ')
        ++lead;
    out.erase(0, lead);
    if (out.size() > 60) {
        size_t cut = 60;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            --cut;
        out.resize(cut);
        while (!out.empty() && (out.back() == '.' || out.back() == ' '))
            out.pop_back();
    }
    static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6",
                                                   "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6",
                                                   "lpt7", "lpt8", "lpt9"};
    if (out.empty() || reserved.count(lowerAscii(out.substr(0, out.find('.')))))
        out = out.empty() ? "plugin" : out + "_";
    return out;
}

fs::path uniqueFile(const fs::path& folder, const std::string& stem, const char* ext)
{
    std::error_code ec;
    fs::path candidate = folder / pathFromUtf8(stem + ext);
    for (int n = 2; fs::exists(candidate, ec) && n < 1000; ++n)
        candidate = folder / pathFromUtf8(stem + " " + std::to_string(n) + ext);
    return candidate;
}

bool writeTextAtomic(const fs::path& file, const std::string& text, std::string* error)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error)
                *error = "Não foi possível gravar em " + paths::toUtf8(file.parent_path());
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            if (error)
                *error = "Falha ao gravar o arquivo (disco cheio?).";
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        if (error)
            *error = "Não foi possível salvar " + paths::toUtf8(file.filename());
        return false;
    }
    return true;
}

bool readTextFile(const fs::path& file, std::string& out, size_t maxBytes)
{
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size > maxBytes)
        return false;
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

namespace {

bool writePlaceholderPng(const fs::path& file)
{
    std::vector<unsigned char> px(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const unsigned char v = static_cast<unsigned char>(((x / 8 + y / 8) & 1) ? 200 : 120);
            unsigned char* p = &px[static_cast<size_t>(y * 64 + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    std::string png;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), static_cast<size_t>(size)); },
        &png, 64, 64, 4, px.data(), 64 * 4);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(png.data(), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(out);
}

}

CreateResult createPlugin(const PluginSpec& spec, const fs::path& folder)
{
    CreateResult res;
    if (const std::string err = validateSpec(spec); !err.empty()) {
        res.error = err;
        return res;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec || !fs::is_directory(folder, ec)) {
        res.error = "Não foi possível criar a pasta " + paths::toUtf8(folder);
        return res;
    }

    const std::string stem = safeFileName(spec.name);
    const fs::path file = uniqueFile(folder, stem);
    const std::string fileStem = paths::toUtf8(file.stem());

    std::vector<std::string> imageNames;
    std::vector<fs::path> imageCopies;
    for (const ParamSpec& p : spec.params) {
        if (p.kind != ParamKind::Image)
            continue;
        std::string ext = ".png";
        fs::path source;
        if (!p.imageFile.empty()) {
            source = pathFromUtf8(p.imageFile);
            ext = lowerAscii(paths::toUtf8(source.extension()));
            if (!isPluginImageExtension(ext)) {
                res.error = "Imagem \"" + p.name + "\": use PNG, JPG, BMP ou TGA.";
                return res;
            }
            const auto size = fs::file_size(source, ec);
            if (ec) {
                res.error = "Não encontrei a imagem escolhida para \"" + p.name + "\".";
                return res;
            }
            if (size > kMaxImageBytes) {
                res.error = "A imagem de \"" + p.name + "\" passa de 8 MB. Reduza o tamanho dela.";
                return res;
            }
        }
        const fs::path target = folder / pathFromUtf8(fileStem + "_" + p.name + ext);
        if (!source.empty()) {
            fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                res.error = "Não foi possível copiar a imagem de \"" + p.name + "\".";
                return res;
            }
        } else if (!writePlaceholderPng(target)) {
            res.error = "Não foi possível criar a imagem de exemplo de \"" + p.name + "\".";
            return res;
        }
        imageCopies.push_back(target);
        imageNames.push_back(paths::toUtf8(target.filename()));
    }

    const std::string text = generateIsf(spec, imageNames);
    if (!writeTextAtomic(file, text, &res.error)) {
        for (const fs::path& img : imageCopies)
            fs::remove(img, ec);
        return res;
    }
    res.ok = true;
    res.file = file;
    return res;
}

CreateResult duplicatePlugin(const fs::path& file, const fs::path& targetFolder)
{
    CreateResult res;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        res.error = "O arquivo do plugin não foi encontrado.";
        return res;
    }
    std::string text;
    if (!readTextFile(file, text, 1u << 20)) {
        res.error = "Não foi possível ler o plugin (arquivo grande demais?).";
        return res;
    }
    std::string stem = paths::toUtf8(file.stem());

    const std::string suffix = " (cópia)";
    if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        stem.resize(stem.size() - suffix.size());
    const bool sameFolder = targetFolder.empty() || fs::equivalent(targetFolder, file.parent_path(), ec);
    const fs::path folder = sameFolder ? file.parent_path() : targetFolder;
    fs::create_directories(folder, ec);
    const fs::path target = uniqueFile(folder, stem + suffix);
    if (!sameFolder) {
        const fx::IsfParseResult parsed = fx::parseIsf(text, "", stem);
        if (parsed.ok)
            for (const fx::IsfImported& im : parsed.doc->imported) {
                const fs::path rel = pathFromUtf8(im.path);
                if (rel.is_absolute() || im.path.find("..") != std::string::npos)
                    continue;
                const fs::path from = file.parent_path() / rel;
                if (!fs::is_regular_file(from, ec))
                    continue;
                fs::create_directories((folder / rel).parent_path(), ec);
                fs::copy_file(from, folder / rel, fs::copy_options::skip_existing, ec);
            }
    }
    if (!writeTextAtomic(target, text, &res.error))
        return res;
    fs::path vs = file;
    vs.replace_extension(".vs");
    if (fs::is_regular_file(vs, ec)) {
        fs::path vsTarget = target;
        vsTarget.replace_extension(".vs");
        fs::copy_file(vs, vsTarget, fs::copy_options::overwrite_existing, ec);
    }
    res.ok = true;
    res.file = target;
    return res;
}

}
