#include "ffgl/FfglImport.h"

#include "app/Log.h"
#include "ffgl/FfglPe.h"

#include <algorithm>
#include <cwctype>

namespace bsl::ffgl {

namespace fs = std::filesystem;

namespace {

std::wstring lowerName(const fs::path& p)
{
    std::wstring s = p.filename().wstring();
    for (wchar_t& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool isDll(const fs::path& p)
{
    std::wstring ext = p.extension().wstring();
    for (wchar_t& c : ext)
        c = static_cast<wchar_t>(std::towlower(c));
    return ext == L".dll";
}

// O plugin WebBrowser do Resolume nunca e importado: o BSL Stage tem a fonte Web nativa
bool isWebBrowserPlugin(const fs::path& p)
{
    return lowerName(p) == L"webbrowser.dll";
}

// A pasta tem algum plugin FFGL (ate 3 niveis)? Pastas assim nao sao "dependencias" de um plugin vizinho.
bool containsPlugins(const fs::path& dir, int depth = 0)
{
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code fec;
        if (it->is_directory(fec)) {
            if (depth < 2 && lowerName(it->path()) != L"webbrowser.cef" && containsPlugins(it->path(), depth + 1))
                return true;
        } else if (it->is_regular_file(fec) && isDll(it->path()) && !isWebBrowserPlugin(it->path())
                   && inspectPe(it->path()) == PeVerdict::Candidate) {
            return true;
        }
    }
    return false;
}

// Copia recursivamente (sobrescreve). Conta falhas por arquivo.
void copyTree(const fs::path& from, const fs::path& to, ImportResult& r)
{
    std::error_code ec;
    fs::create_directories(to, ec);
    for (fs::directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code fec;
        const fs::path target = to / it->path().filename();
        if (it->is_directory(fec)) {
            copyTree(it->path(), target, r);
        } else if (it->is_regular_file(fec)) {
            if (isWebBrowserPlugin(it->path()))
                continue;
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, fec);
            if (fec) {
                ++r.failures;
                log::warn("Importar plugins: não foi possível copiar \"" + it->path().filename().string() + "\": " + fec.message());
            } else if (isDll(it->path()) && inspectPe(it->path()) == PeVerdict::Candidate) {
                ++r.dlls;
            }
        }
    }
}

std::string summary(const ImportResult& r, bool webBrowserSkipped)
{
    std::string m;
    if (r.dlls == 0 && r.failures == 0)
        m = webBrowserSkipped ? "O WebBrowser não é importado: use a fonte Web nativa (Fontes > Web)."
                              : "Nenhum plugin FFGL x64 encontrado.";
    else
        m = std::to_string(r.dlls) + " plugin(s) importado(s)" +
            (r.folders > 0 ? ", " + std::to_string(r.folders) + " pasta(s) de apoio" : std::string());
    if (r.failures > 0)
        m += "; " + std::to_string(r.failures) + " arquivo(s) não puderam ser copiados (em uso?)";
    return m;
}

} // namespace

ImportResult importPlugins(const fs::path& source, const fs::path& dest)
{
    ImportResult r;
    std::error_code ec;
    try {
        fs::create_directories(dest, ec);
        bool webBrowserSkipped = false;
        if (fs::is_directory(source, ec)) {
            // Uma pasta: vai inteira (com as subpastas) para Plugins\<nome da pasta>
            const fs::path target = dest / source.filename();
            copyTree(source, target, r);
            ++r.folders;
        } else if (fs::is_regular_file(source, ec) && isDll(source)) {
            if (isWebBrowserPlugin(source)) {
                webBrowserSkipped = true;
            } else if (inspectPe(source) != PeVerdict::Candidate) {
                r.message = "Esta DLL não é um plugin FFGL x64 (não exporta plugMain).";
                return r;
            } else {
                std::error_code fec;
                fs::copy_file(source, dest / source.filename(), fs::copy_options::overwrite_existing, fec);
                if (fec)
                    ++r.failures;
                else
                    ++r.dlls;
                // Subpastas ao lado da DLL que nao tem outros plugins sao as dependencias dela
                for (fs::directory_iterator it(source.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
                    std::error_code dec;
                    if (!it->is_directory(dec) || containsPlugins(it->path()))
                        continue;
                    copyTree(it->path(), dest / it->path().filename(), r);
                    ++r.folders;
                }
            }
        } else {
            r.message = "Escolha uma DLL de plugin ou uma pasta.";
            return r;
        }
        r.message = summary(r, webBrowserSkipped);
    } catch (const std::exception& e) {
        r.failures++;
        r.message = std::string("Falha ao importar: ") + e.what();
    }
    return r;
}

ImportJob::~ImportJob()
{
    if (thread_.joinable())
        thread_.join();
}

void ImportJob::start(const fs::path& source, const fs::path& dest)
{
    if (running_.load())
        return;
    if (thread_.joinable())
        thread_.join();
    running_ = true;
    thread_ = std::thread([this, source, dest] {
        ImportResult r = importPlugins(source, dest);
        {
            std::lock_guard lock(mutex_);
            result_ = std::move(r);
            have_ = true;
        }
        running_ = false;
    });
}

bool ImportJob::takeResult(ImportResult& out)
{
    std::lock_guard lock(mutex_);
    if (!have_)
        return false;
    out = result_;
    have_ = false;
    return true;
}

} // namespace bsl::ffgl
