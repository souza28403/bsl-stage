#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>

#if BSL_WITH_D3D
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#else
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct IDXGIAdapter1;
#endif

namespace bsl::render {

class D3D11Interop;

#if !BSL_WITH_D3D

class InteropTexture
{
public:
    ~InteropTexture();
    InteropTexture(const InteropTexture&) = delete;
    InteropTexture& operator=(const InteropTexture&) = delete;

    ID3D11Texture2D* d3d() const { return nullptr; }
    ID3D11RenderTargetView* rtv() const { return nullptr; }
    GLuint gl() const { return 0; }
    int width() const { return 0; }
    int height() const { return 0; }
    bool usesInterop() const { return false; }

    void beginD3D() {}
    void endD3D() {}
    void clear(float, float, float, float) {}

private:
    InteropTexture() = default;
};

class D3D11Interop
{
public:
    static std::shared_ptr<D3D11Interop> create(std::string* error = nullptr);
    static std::shared_ptr<D3D11Interop> shared(std::string* error = nullptr);

    ~D3D11Interop();
    D3D11Interop(const D3D11Interop&) = delete;
    D3D11Interop& operator=(const D3D11Interop&) = delete;

    ID3D11Device* device() const { return nullptr; }
    ID3D11DeviceContext* context() const { return nullptr; }
    IDXGIAdapter1* adapter() const { return nullptr; }
    bool interopAvailable() const { return false; }
    const std::string& adapterName() const { return adapterName_; }
    const std::string& glRenderer() const { return glRenderer_; }
    std::string describe() const;

    std::unique_ptr<InteropTexture> createTexture(int width, int height, bool forceCpu = false, int format = 0,
                                                  bool keepAlpha = false);

private:
    D3D11Interop() = default;

    std::string adapterName_;
    std::string glRenderer_;
};

#else


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
    void clear(float r, float g, float b, float a);

private:
    friend class D3D11Interop;
    InteropTexture() = default;

    D3D11Interop* owner_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    GLuint glTex_ = 0;
    void* shareHandle_ = nullptr;
    bool locked_ = false;
    int width_ = 0;
    int height_ = 0;
};

class D3D11Interop
{
public:
    static std::shared_ptr<D3D11Interop> create(std::string* error = nullptr);
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
    std::string describe() const;

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
    void* interopDevice_ = nullptr;
    std::string adapterName_;
    std::string glRenderer_;
};

#endif

}
