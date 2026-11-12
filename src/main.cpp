#include "app/App.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>

namespace {

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
            const wchar_t* mode = argv[++i];
            options.startInMapping = std::wcscmp(mode, L"mapping") == 0;
            options.startInPixelMap = std::wcscmp(mode, L"pixelmap") == 0;
            options.startInSequences = std::wcscmp(mode, L"sequences") == 0;
        } else if (argv[i][0] != L'-' && options.projectPath.empty()) {
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
        } else if (std::wcscmp(argv[i], L"--tab") == 0 && i + 1 < argc) {
            for (const wchar_t* p = argv[++i]; *p; ++p)
                options.focusTab += static_cast<char>(*p);
        } else if (std::wcscmp(argv[i], L"--fake-release") == 0 && i + 1 < argc) {
            options.fakeRelease = argv[++i];
        } else if (std::wcscmp(argv[i], L"--ndi-lib") == 0 && i + 1 < argc) {
            options.ndiLib = argv[++i];
        } else if (std::wcscmp(argv[i], L"--plugins") == 0 && i + 1 < argc) {
            options.pluginFolders.emplace_back(argv[++i]);
        } else if (std::wcscmp(argv[i], L"--fx-open") == 0 && i + 1 < argc) {
            for (const wchar_t* p = argv[++i]; *p; ++p)
                options.fxOpen += static_cast<char>(*p);
        } else if (std::wcscmp(argv[i], L"--dmx-demo") == 0 && i + 1 < argc) {
            for (const wchar_t* p = argv[++i]; *p; ++p)
                options.dmxDemo += static_cast<char>(*p);
        } else if (std::wcscmp(argv[i], L"--choose-target") == 0) {
            options.openCaptureChooser = true;
        } else if (std::wcscmp(argv[i], L"--identify") == 0) {
            options.identify = true;
        } else if (std::wcscmp(argv[i], L"--test-missing-monitor") == 0) {
            options.testMissingMonitor = true;
        } else if (std::wcscmp(argv[i], L"--output-window") == 0) {
            options.outputWindow = true;
        } else if (std::wcscmp(argv[i], L"--mode") == 0 && i + 1 < argc) {
            const wchar_t* mode = argv[++i];
            options.startInMapping = std::wcscmp(mode, L"mapping") == 0;
            options.startInPixelMap = std::wcscmp(mode, L"pixelmap") == 0;
            options.startInSequences = std::wcscmp(mode, L"sequences") == 0;
        }
    }
    LocalFree(argv);
    return options;
}

}

int main()
{
    bsl::App app(parseArgs());
    return app.run();
}
