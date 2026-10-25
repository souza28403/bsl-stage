#pragma once

#include "media/ISource.h"

#include <memory>

namespace bsl::media {

enum class CaptureBackend { Auto, WindowsGraphicsCapture, DesktopDuplication };

// Captura de tela: monitor N ou janela especifica. Windows Graphics Capture (C++/WinRT) para os dois; se o
// WGC nao estiver disponivel e o alvo for um monitor, cai para o DXGI Desktop Duplication (sem cursor).
// O quadro D3D11 chega ao GL por render/D3D11Interop (WGL_NV_DX_interop2, com copia por CPU de reserva).
// Fonte "ao vivo": a Previa nao abre uma segunda instancia.
class ScreenCaptureSource final : public ISource
{
public:
    ScreenCaptureSource();
    ~ScreenCaptureSource() override;

    bool open(const std::filesystem::path& path, std::string* error = nullptr) override;
    void close() override;
    bool isOpen() const override { return open_; }
    void update(double dt) override;

    GLuint texture() const override;
    int width() const override;
    int height() const override;
    bool hasAlpha() const override { return false; }
    bool supportsReverse() const override { return false; }

    std::vector<ParamDesc> paramDescs() const override { return describe(values_); }
    bool isLive() const override { return true; }
    SourceStatus status() const override;

    static std::vector<ParamDesc> describe(const ParamValues& values);
    static std::string label(const ParamValues& values);

    // --- Diagnostico / testes ---
    std::string backendName() const; // "Windows Graphics Capture", "DXGI Desktop Duplication" ou ""
    std::string pathName() const;    // "interop (GPU)" ou "cópia por CPU" ou ""
    bool usesInterop() const;
    unsigned long long frameCount() const;
    // Testes: forca o backend / o caminho de CPU para as proximas aberturas
    static void forceBackend(CaptureBackend backend);
    static void forceCpuPath(bool cpu);

protected:
    void onParamsChanged() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool open_ = false;
};

} // namespace bsl::media
