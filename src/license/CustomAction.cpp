#include "license/Licenca.h"

#include <windows.h>
#include <msiquery.h>

#include <string>

namespace {

std::string lerPropriedade(MSIHANDLE msi, const wchar_t* nome)
{
    DWORD tamanho = 0;
    wchar_t vazio[1] = {0};
    UINT r = MsiGetPropertyW(msi, nome, vazio, &tamanho);
    if (r != ERROR_MORE_DATA && r != ERROR_SUCCESS)
        return {};
    std::wstring larga(static_cast<std::size_t>(tamanho) + 1, L'\0');
    tamanho = static_cast<DWORD>(larga.size());
    if (MsiGetPropertyW(msi, nome, larga.data(), &tamanho) != ERROR_SUCCESS)
        return {};
    larga.resize(tamanho);
    if (larga.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, larga.data(), static_cast<int>(larga.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string utf8(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, larga.data(), static_cast<int>(larga.size()), utf8.data(), n, nullptr, nullptr);
    return utf8;
}

}

extern "C" __declspec(dllexport) UINT __stdcall VerificarLicenca(MSIHANDLE msi)
{
    const std::string email = lerPropriedade(msi, L"BSL_EMAIL");
    const std::string chave = lerPropriedade(msi, L"BSL_CHAVE");
    const bool ok = bsl::licenca::verificarChave(email, chave);
    MsiSetPropertyW(msi, L"BSL_LICENCA_OK", ok ? L"1" : L"0");
    return ERROR_SUCCESS;
}
