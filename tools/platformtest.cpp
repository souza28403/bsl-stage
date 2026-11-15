#include "platform/DynLib.h"
#include "platform/ExePath.h"
#include "platform/Paths.h"
#include "platform/Utf.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "ok" : "FALHOU", what);
    if (!ok)
        ++g_failures;
}

}

int main()
{
    const std::string accents = "a\xC3\xA7\xC3\xA3o \xE2\x82\xAC \xF0\x9F\x8E\xAC";
    const std::wstring wide = bslplat::utf8ToWide(accents);
    check(!wide.empty(), "utf8 para wide nao vazio");
    check(bslplat::wideToUtf8(wide) == accents, "ida e volta utf8 com acentos e emoji");
    check(bslplat::utf8ToWide("abc") == L"abc", "ascii");
    check(bslplat::utf8ToWide(std::string("\xFF\xC3", 2)) == std::wstring(2, wchar_t(0xFFFD)), "bytes invalidos viram substituto");
    check(bslplat::wideToUtf8(L"") .empty(), "vazio");

    const std::filesystem::path home = "/home/u";
    check(bslplat::xdgDir("/x/cfg", home, ".config") == std::filesystem::path("/x/cfg"), "xdg usa variavel absoluta");
    check(bslplat::xdgDir("", home, ".config") == home / ".config", "xdg reserva sem variavel");
    check(bslplat::xdgDir("relativo", home, ".cache") == home / ".cache", "xdg ignora variavel relativa");

    check(!bslplat::configDir().empty(), "configDir");
    check(!bslplat::dataDir().empty(), "dataDir");
    check(!bslplat::cacheDir().empty(), "cacheDir");
    check(!bslplat::documentsDir().empty(), "documentsDir");

    const std::filesystem::path exe = bslplat::exePath();
    std::error_code ec;
    check(!exe.empty() && std::filesystem::exists(exe, ec), "exePath existe");
    check(bslplat::exeDir() == exe.parent_path(), "exeDir");

    bslplat::DynLib none;
    check(!none.open("bsl-biblioteca-inexistente"), "biblioteca inexistente falha");
    check(none.symbol("x") == nullptr, "simbolo sem biblioteca");
#ifdef _WIN32
    bslplat::DynLib k;
    check(k.open("kernel32.dll"), "abre kernel32");
    check(k.symbol("GetTickCount") != nullptr, "resolve simbolo");
    check(k.symbol("BslNaoExiste") == nullptr, "simbolo inexistente");
#else
    bslplat::DynLib k;
    check(k.open("libc.so.6") || k.open("libSystem.dylib"), "abre libc");
    check(k.symbol("strlen") != nullptr, "resolve simbolo");
#endif

    std::printf("%s\n", g_failures == 0 ? "platformtest: tudo certo" : "platformtest: falhas");
    return g_failures == 0 ? 0 : 1;
}
