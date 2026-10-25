#pragma once

// Textura D3D11 -> OpenGL, reutilizavel (captura de tela agora; navegador/WebView na Fase 3B).
//
//  * Caminho rapido: WGL_NV_DX_interop2. A textura D3D11 e a textura GL sao a mesma memoria; so ha
//    bloqueio/desbloqueio entre as duas APIs.
//  * Fallback automatico: copia pela CPU (textura staging -> glTexSubImage2D) quando a extensao nao existe
//    ou o teste de registro falha.
//
// Optimus / varias GPUs: o device D3D11 precisa estar no mesmo adaptador do contexto GL. O adaptador e
// escolhido pelo fabricante do GL_VENDOR e confirmado testando o interop de verdade; se nenhum adaptador
// passar no teste, o device fica no adaptador do mesmo fabricante e tudo vai pelo fallback de CPU.
//
// Tudo isto roda na thread do contexto GL (o contexto principal precisa estar corrente).

#include <glad/gl.h>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <memory>
#include <string>

namespace bsl::render {

class D3D11Interop;

// Textura D3D11 (DEFAULT, BGRA8 por padrao) com uma textura GL equivalente.
// Para escrever pelo D3D: beginD3D() -> comandos no context() usando d3d() -> endD3D(). Depois disso
// gl() mostra o conteudo novo (e continua valida ate o proximo beginD3D()).
class InteropTexture
{
public:
    ~InteropTexture();
    InteropTexture(const InteropTexture&) = delete;
    InteropTexture& operator=(const InteropTexture&) = delete;

    ID3D11Texture2D* d3d() const { return tex_.Get(); }
    ID3D11RenderTargetView* rtv() const { return rtv_.Get(); }
    GLuint gl() const { return glTex_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool usesInterop() const { return shareHandle_ != nullptr; }

    void beginD3D();
    void endD3D();
    // Preenche com uma cor (inclui begin/end)
    void clear(float r, float g, float b, float a);

private:
    friend class D3D11Interop;
    InteropTexture() = default;

    D3D11Interop* owner_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_; // so no fallback
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    GLuint glTex_ = 0;
    void* shareHandle_ = nullptr; // HANDLE do objeto registrado no interop (null = fallback CPU)
    bool locked_ = false;
    int width_ = 0;
    int height_ = 0;
};

class D3D11Interop
{
public:
    // Cria o device no adaptador do contexto GL corrente. nullptr (e 'error') se nao houver D3D11.
    // A falta do interop NAO e erro: usesInterop() fica false e tudo cai no fallback de CPU.
    static std::shared_ptr<D3D11Interop> create(std::string* error = nullptr);
    // Instancia compartilhada pelo processo (criada sob demanda; some quando ninguem a usa mais)
    static std::shared_ptr<D3D11Interop> shared(std::string* error = nullptr);

    ~D3D11Interop();
    D3D11Interop(const D3D11Interop&) = delete;
    D3D11Interop& operator=(const D3D11Interop&) = delete;

    ID3D11Device* device() const { return device_.Get(); }
    ID3D11DeviceContext* context() const { return context_.Get(); }
    IDXGIAdapter1* adapter() const { return adapter_.Get(); }
    bool interopAvailable() const { return interopDevice_ != nullptr; }
    const std::string& adapterName() const { return adapterName_; }
    const std::string& glRenderer() const { return glRenderer_; }
    // Texto de uma linha para o log: adaptador, GL e caminho em uso
    std::string describe() const;

    // 'forceCpu' ignora o interop (testes do fallback)
    // 'keepAlpha': conserva o canal alpha da textura (por padrao ele e forcado a 1: a captura de tela e opaca)
    std::unique_ptr<InteropTexture> createTexture(int width, int height, bool forceCpu = false,
                                                  DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM, bool keepAlpha = false);

private:
    friend class InteropTexture;
    D3D11Interop() = default;

    bool initOnAdapter(IDXGIAdapter1* adapter, bool testInterop);
    bool openInterop();
    void closeInterop();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
    void* interopDevice_ = nullptr; // HANDLE de wglDXOpenDeviceNV
    std::string adapterName_;
    std::string glRenderer_;
};

} // namespace bsl::render
