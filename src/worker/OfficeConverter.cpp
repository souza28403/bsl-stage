#include "worker/OfficeConverter.h"

#include "worker/ComAuto.h"
#include "worker/WorkerUtil.h"

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace bsl::worker {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNoOfficeMessage = "Instale o Microsoft Office ou o LibreOffice (gratuito) para abrir este arquivo";

std::string cacheKey(const fs::path& source)
{
    std::error_code ec;
    const fs::path abs = fs::absolute(source, ec).lexically_normal();
    std::wstring lower = abs.wstring();
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    const auto size = fs::file_size(abs, ec);
    const auto mtime = fs::last_write_time(abs, ec).time_since_epoch().count();
    return toUtf8(lower) + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|"
           + std::to_string(static_cast<long long>(mtime));
}

// Mutex nomeado: uma conversao por vez (o Office e uma instancia unica e o Quit de uma atrapalharia a outra)
class NamedLock
{
public:
    NamedLock(const std::wstring& name, DWORD timeoutMs)
    {
        h_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (h_) {
            const DWORD r = WaitForSingleObject(h_, timeoutMs);
            held_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
        }
    }
    ~NamedLock()
    {
        if (h_) {
            if (held_)
                ReleaseMutex(h_);
            CloseHandle(h_);
        }
    }
    NamedLock(const NamedLock&) = delete;
    NamedLock& operator=(const NamedLock&) = delete;
    bool held() const { return held_; }

private:
    HANDLE h_ = nullptr;
    bool held_ = false;
};

enum class OfficeApp { PowerPoint, Word, Excel };

OfficeApp appFor(const std::string& ext)
{
    if (ext == "pptx" || ext == "ppt" || ext == "odp")
        return OfficeApp::PowerPoint;
    if (ext == "docx" || ext == "doc" || ext == "odt")
        return OfficeApp::Word;
    return OfficeApp::Excel;
}

const wchar_t* progIdFor(OfficeApp a)
{
    switch (a) {
    case OfficeApp::PowerPoint: return L"PowerPoint.Application";
    case OfficeApp::Word: return L"Word.Application";
    case OfficeApp::Excel: return L"Excel.Application";
    }
    return L"";
}

const wchar_t* exeFor(OfficeApp a)
{
    switch (a) {
    case OfficeApp::PowerPoint: return L"POWERPNT.EXE";
    case OfficeApp::Word: return L"WINWORD.EXE";
    case OfficeApp::Excel: return L"EXCEL.EXE";
    }
    return L"";
}

bool officeInstalled(OfficeApp a)
{
    CLSID clsid{};
    return SUCCEEDED(CLSIDFromProgID(progIdFor(a), &clsid));
}

bool pidAlive(DWORD pid)
{
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

void trySet(const com::Object& o, const wchar_t* prop, const com::Var& v)
{
    try {
        o.put(prop, v);
    } catch (const com::ComError&) {
        // PowerPoint nao deixa esconder a janela e o Word/Excel podem recusar: nao e fatal
    }
}

void tryCall(const com::Object& o, const wchar_t* method, std::initializer_list<const com::Var*> args = {})
{
    try {
        o.call(method, args);
    } catch (...) {
    }
}

// Abre, exporta e fecha o documento com o aplicativo ja obtido. Lanca ComError.
void exportWithOffice(OfficeApp which, const com::Object& app, const fs::path& source, const fs::path& outPdf)
{
    using com::Var;
    const Var in = Var::text(source.wstring());
    const Var out = Var::text(outPdf.wstring());
    switch (which) {
    case OfficeApp::PowerPoint: {
        trySet(app, L"DisplayAlerts", Var::integer(1)); // ppAlertsNone
        const Var ro = Var::mso(true), untitled = Var::mso(false), noWindow = Var::mso(false);
        const com::Object presentations = app.getObject(L"Presentations");
        const Var opened = presentations.call(L"Open", {&in, &ro, &untitled, &noWindow});
        const com::Object pres(opened.dispatch(), true);
        if (!pres)
            throw com::ComError("o PowerPoint não abriu o arquivo", E_FAIL);
        try {
            const Var fmt = Var::integer(32); // ppSaveAsPDF
            pres.call(L"SaveAs", {&out, &fmt});
        } catch (...) {
            tryCall(pres, L"Close");
            throw;
        }
        tryCall(pres, L"Close");
        break;
    }
    case OfficeApp::Word: {
        trySet(app, L"DisplayAlerts", Var::integer(0)); // wdAlertsNone
        trySet(app, L"Visible", Var::boolean(false));
        const Var noConfirm = Var::boolean(false), ro = Var::boolean(true), noRecent = Var::boolean(false);
        const com::Object documents = app.getObject(L"Documents");
        const Var opened = documents.call(L"Open", {&in, &noConfirm, &ro, &noRecent});
        const com::Object doc(opened.dispatch(), true);
        if (!doc)
            throw com::ComError("o Word não abriu o arquivo", E_FAIL);
        const Var noSave = Var::integer(0); // wdDoNotSaveChanges
        try {
            const Var fmt = Var::integer(17); // wdExportFormatPDF
            doc.call(L"ExportAsFixedFormat", {&out, &fmt});
        } catch (...) {
            tryCall(doc, L"Close", {&noSave});
            throw;
        }
        tryCall(doc, L"Close", {&noSave});
        break;
    }
    case OfficeApp::Excel: {
        trySet(app, L"DisplayAlerts", Var::boolean(false));
        trySet(app, L"Visible", Var::boolean(false));
        const Var noLinks = Var::integer(0), ro = Var::boolean(true);
        const com::Object workbooks = app.getObject(L"Workbooks");
        const Var opened = workbooks.call(L"Open", {&in, &noLinks, &ro});
        const com::Object wb(opened.dispatch(), true);
        if (!wb)
            throw com::ComError("o Excel não abriu o arquivo", E_FAIL);
        const Var noSave = Var::boolean(false);
        try {
            const Var type = Var::integer(0); // xlTypePDF
            wb.call(L"ExportAsFixedFormat", {&type, &out});
        } catch (...) {
            tryCall(wb, L"Close", {&noSave});
            throw;
        }
        tryCall(wb, L"Close", {&noSave});
        break;
    }
    }
}

// Converte pelo Office. Devolve vazio se deu certo; senao o motivo.
std::string convertWithOffice(OfficeApp which, const fs::path& source, const fs::path& outPdf)
{
    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::string error;
    const std::vector<DWORD> before = processIds(exeFor(which));
    std::vector<DWORD> mine;
    bool quit = false;
    {
        std::string attachError;
        com::AppInstance inst = com::attachOrCreate(progIdFor(which), &attachError);
        if (!inst.app) {
            error = attachError;
        } else {
            if (inst.createdByUs) {
                // Processos novos = os que iniciamos (so esses podemos encerrar)
                for (int i = 0; i < 40 && mine.empty(); ++i) {
                    for (DWORD pid : processIds(exeFor(which)))
                        if (std::find(before.begin(), before.end(), pid) == before.end())
                            mine.push_back(pid);
                    if (mine.empty())
                        Sleep(50);
                }
            }
            quit = inst.createdByUs && !mine.empty();
            try {
                exportWithOffice(which, inst.app, source, outPdf);
            } catch (const std::exception& e) {
                error = e.what();
            }
            if (quit)
                tryCall(inst.app, L"Quit");
        }
    } // objetos COM liberados aqui, antes de esperar o processo sair

    if (quit) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            bool any = false;
            for (DWORD pid : mine)
                any = any || pidAlive(pid);
            if (!any || std::chrono::steady_clock::now() > deadline)
                break;
            Sleep(100);
        }
        for (DWORD pid : mine) { // encerra so o que nos mesmos iniciamos e que nao quis sair
            if (!pidAlive(pid))
                continue;
            if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
                TerminateProcess(h, 1);
                CloseHandle(h);
            }
        }
    }
    if (SUCCEEDED(initHr))
        CoUninitialize();
    return error;
}

// --- LibreOffice ---

fs::path findSoffice()
{
    std::vector<fs::path> candidates;
    HKEY k = nullptr;
    for (const wchar_t* sub : {L"SOFTWARE\\LibreOffice\\UNO\\InstallPath", L"SOFTWARE\\WOW6432Node\\LibreOffice\\UNO\\InstallPath"}) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH * 2];
            DWORD size = sizeof(buf), type = 0;
            if (RegQueryValueExW(k, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS
                && type == REG_SZ)
                candidates.push_back(fs::path(buf) / L"soffice.exe");
            RegCloseKey(k);
        }
    }
    for (const wchar_t* env : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"}) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(env, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
            candidates.push_back(fs::path(buf) / L"LibreOffice" / L"program" / L"soffice.exe");
    }
    std::error_code ec;
    for (const fs::path& c : candidates)
        if (fs::is_regular_file(c, ec))
            return c;
    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"soffice.exe", nullptr, MAX_PATH, found, nullptr) > 0)
        return found;
    return {};
}

std::string convertWithLibreOffice(const fs::path& soffice, const fs::path& source, const fs::path& outPdf)
{
    std::error_code ec;
    const fs::path tmpDir = outPdf.parent_path() / (outPdf.stem().wstring() + L".lo");
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    const fs::path profile = localDataDir() / L"cache" / L"lo-profile";
    fs::create_directories(profile, ec);

    std::wstring cmd = L"\"" + soffice.wstring() + L"\" --headless --norestore --nolockcheck --nologo"
                       L" \"-env:UserInstallation=file:///" + profile.generic_wstring() + L"\""
                       L" --convert-to pdf --outdir \"" + tmpDir.wstring() + L"\" \"" + source.wstring() + L"\"";

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        nullptr, &si, &pi)) {
        if (job)
            CloseHandle(job);
        return "não foi possível iniciar o LibreOffice (erro " + std::to_string(GetLastError()) + ")";
    }
    if (job)
        AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    std::string error;
    if (WaitForSingleObject(pi.hProcess, 180000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        error = "o LibreOffice demorou demais para converter";
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (job)
        CloseHandle(job); // mata o soffice.bin, se sobrou

    const fs::path produced = tmpDir / (source.stem().wstring() + L".pdf");
    if (error.empty()) {
        if (fs::is_regular_file(produced, ec))
            fs::rename(produced, outPdf, ec);
        if (ec || !fs::is_regular_file(outPdf, ec))
            error = "o LibreOffice não gerou o PDF";
    }
    fs::remove_all(tmpDir, ec);
    return error;
}

} // namespace

fs::path docCachePath(const fs::path& source)
{
    return localDataDir() / L"cache" / L"docs" / (fromUtf8(fnv1aHex(cacheKey(source))) + L".pdf");
}

ConvertResult convertToPdf(const fs::path& source)
{
    ConvertResult r;
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        r.error = "arquivo não encontrado";
        return r;
    }
    const fs::path target = docCachePath(source);
    fs::create_directories(target.parent_path(), ec);

    auto cached = [&] { return fs::is_regular_file(target, ec) && fs::file_size(target, ec) > 0; };
    auto fromCache = [&] {
        r.ok = true;
        r.pdf = target;
        r.fromCache = true;
        r.via = "cache";
        return r;
    };
    if (cached())
        return fromCache();

    const OfficeApp which = appFor(lowerExt(source));
    // Uma conversao por vez no sistema inteiro; quem esperou normalmente ja encontra o cache pronto
    NamedLock lock(L"Local\\bslstage-office-convert", 240000);
    if (cached())
        return fromCache();

    const fs::path tmp = target.parent_path() / (target.stem().wstring() + L".tmp.pdf");
    fs::remove(tmp, ec);

    std::string officeError;
    const bool officeThere = officeInstalled(which);
    if (officeThere) {
        officeError = convertWithOffice(which, source, tmp);
        if (officeError.empty() && fs::is_regular_file(tmp, ec) && fs::file_size(tmp, ec) > 0) {
            fs::rename(tmp, target, ec);
            if (!ec) {
                r.ok = true;
                r.pdf = target;
                r.via = "Microsoft Office";
                return r;
            }
            officeError = "não foi possível gravar o PDF no cache";
        } else if (officeError.empty()) {
            officeError = "o Office não gerou o PDF";
        }
        fs::remove(tmp, ec);
    }

    const fs::path soffice = findSoffice();
    std::string loError;
    if (!soffice.empty()) {
        loError = convertWithLibreOffice(soffice, source, tmp);
        if (loError.empty()) {
            fs::rename(tmp, target, ec);
            if (!ec) {
                r.ok = true;
                r.pdf = target;
                r.via = "LibreOffice";
                return r;
            }
            loError = "não foi possível gravar o PDF no cache";
        }
        fs::remove(tmp, ec);
    }

    if (soffice.empty() && !officeThere) {
        r.error = kNoOfficeMessage;
    } else {
        r.error = "Não foi possível converter o arquivo";
        if (!officeError.empty())
            r.error += " (Microsoft Office: " + officeError + ")";
        if (!loError.empty())
            r.error += " (LibreOffice: " + loError + ")";
    }
    return r;
}

} // namespace bsl::worker
