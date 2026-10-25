#include "app/App.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>

namespace {

// Linha de comando:  --demo <pasta>   --screenshot <arquivo.png> <segundos>
//                    --output-window   --screenshot-output <arquivo.png> <segundos>   --mode mapping
//                    --identify   --test-missing-monitor   --window-size <w> <h>
//                    --trigger <camada> <coluna>   (testes: dispara o clip depois do --demo; repetivel)
//                    --ui-scale <f>   --ui-mode window|system|fixed   (escala da interface, para testes)
//                    --qa <pasta>   (teste de interface: ver app/Qa.h e tools/uiqa.py)
//                    <projeto>.bslstage   (primeiro argumento posicional: abre o projeto)
bsl::AppOptions parseArgs()
{
    bsl::AppOptions options;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return options;

    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--demo") == 0 && i + 1 < argc) {
            options.demoFolder = argv[++i];
        } else if (std::wcscmp(argv[i], L"--screenshot") == 0 && i + 2 < argc) {
            options.screenshotPath = argv[++i];
            options.screenshotSeconds = std::wcstod(argv[++i], nullptr);
        } else if (std::wcscmp(argv[i], L"--screenshot-output") == 0 && i + 2 < argc) {
            options.screenshotOutputPath = argv[++i];
            options.screenshotOutputSeconds = std::wcstod(argv[++i], nullptr);
        } else if (std::wcscmp(argv[i], L"--output-window") == 0) {
            options.outputWindow = true;
        } else if (std::wcscmp(argv[i], L"--mode") == 0 && i + 1 < argc) {
            options.startInMapping = std::wcscmp(argv[++i], L"mapping") == 0;
        } else if (argv[i][0] != L'-' && options.projectPath.empty()) {
            // Primeiro argumento posicional <projeto>.bslstage
            const std::filesystem::path file(argv[i]);
            if (_wcsicmp(file.extension().c_str(), L".bslstage") == 0)
                options.projectPath = file;
        } else if (std::wcscmp(argv[i], L"--screenshot-output") == 0 && i + 2 < argc) {
            options.screenshotOutputPath = argv[++i];
            options.screenshotOutputSeconds = std::wcstod(argv[++i], nullptr);
        } else if (std::wcscmp(argv[i], L"--canvas") == 0 && i + 2 < argc) {
            options.canvasWidth = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
            options.canvasHeight = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        } else if (std::wcscmp(argv[i], L"--window-size") == 0 && i + 2 < argc) {
            options.windowWidth = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
            options.windowHeight = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        } else if (std::wcscmp(argv[i], L"--trigger") == 0 && i + 2 < argc) {
            const int layer = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
            const int column = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
            options.triggers.emplace_back(layer, column);
        } else if (std::wcscmp(argv[i], L"--ui-scale") == 0 && i + 1 < argc) {
            options.uiScale = static_cast<float>(std::wcstod(argv[++i], nullptr));
        } else if (std::wcscmp(argv[i], L"--ui-mode") == 0 && i + 1 < argc) {
            const std::wstring mode = argv[++i];
            options.uiMode = mode == L"window" ? "window" : mode == L"system" ? "system" : mode == L"fixed" ? "fixed" : "";
        } else if (std::wcscmp(argv[i], L"--qa") == 0 && i + 1 < argc) {
            options.qaDir = argv[++i];
        } else if (std::wcscmp(argv[i], L"--choose-target") == 0) {
            options.openCaptureChooser = true;
        } else if (std::wcscmp(argv[i], L"--identify") == 0) {
            options.identify = true;
        } else if (std::wcscmp(argv[i], L"--test-missing-monitor") == 0) {
            options.testMissingMonitor = true;
        } else if (std::wcscmp(argv[i], L"--output-window") == 0) {
            options.outputWindow = true;
        } else if (std::wcscmp(argv[i], L"--mode") == 0 && i + 1 < argc) {
            options.startInMapping = std::wcscmp(argv[++i], L"mapping") == 0;
        }
    }
    LocalFree(argv);
    return options;
}

} // namespace

int main()
{
    bsl::App app(parseArgs());
    return app.run();
}
