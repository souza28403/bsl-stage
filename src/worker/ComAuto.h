#pragma once

#include <windows.h>
#include <oleauto.h>

#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

namespace bsl::com {

struct ComError : std::runtime_error
{
    HRESULT hr;
    ComError(const std::string& what, HRESULT h) : std::runtime_error(what), hr(h) {}
};

inline std::string hrText(HRESULT hr)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

inline std::string toUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

class Var
{
public:
    Var() { VariantInit(&v_); }
    ~Var() { VariantClear(&v_); }
    Var(const Var& o) { VariantInit(&v_); VariantCopy(&v_, const_cast<VARIANT*>(&o.v_)); }
    Var(Var&& o) noexcept { v_ = o.v_; VariantInit(&o.v_); }
    Var& operator=(Var o) noexcept { std::swap(v_, o.v_); return *this; }

    static Var integer(long n) { Var x; x.v_.vt = VT_I4; x.v_.lVal = n; return x; }
    static Var boolean(bool b) { Var x; x.v_.vt = VT_BOOL; x.v_.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return x; }
    static Var text(const std::wstring& s) { Var x; x.v_.vt = VT_BSTR; x.v_.bstrVal = SysAllocStringLen(s.data(), static_cast<UINT>(s.size())); return x; }
    static Var mso(bool b) { return integer(b ? -1 : 0); }

    VARIANT* get() { return &v_; }
    const VARIANT* get() const { return &v_; }
    IDispatch* dispatch() const { return (v_.vt == VT_DISPATCH) ? v_.pdispVal : nullptr; }
    long asLong() const { return (v_.vt == VT_I4) ? v_.lVal : (v_.vt == VT_I2 ? v_.iVal : 0); }

private:
    VARIANT v_;
};

class Object
{
public:
    Object() = default;
    explicit Object(IDispatch* p, bool addRef = false) : p_(p) { if (p_ && addRef) p_->AddRef(); }
    Object(const Object& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Object(Object&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    Object& operator=(Object o) noexcept { std::swap(p_, o.p_); return *this; }
    ~Object() { reset(); }

    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    explicit operator bool() const { return p_ != nullptr; }
    IDispatch* get() const { return p_; }

    Var call(const wchar_t* name, std::initializer_list<const Var*> args = {}) const
    {
        return invoke(DISPATCH_METHOD, name, args);
    }
    Var get(const wchar_t* name, std::initializer_list<const Var*> args = {}) const
    {
        return invoke(static_cast<WORD>(DISPATCH_PROPERTYGET | DISPATCH_METHOD), name, args);
    }
    Object getObject(const wchar_t* name, std::initializer_list<const Var*> args = {}) const
    {
        Var v = get(name, args);
        return Object(v.dispatch(), true);
    }
    void put(const wchar_t* name, const Var& value) const
    {
        DISPID dispid = lookup(name);
        DISPID named = DISPID_PROPERTYPUT;
        DISPPARAMS dp{};
        VARIANT arg = *value.get();
        dp.cArgs = 1;
        dp.rgvarg = &arg;
        dp.cNamedArgs = 1;
        dp.rgdispidNamedArgs = &named;
        EXCEPINFO ex{};
        UINT argErr = 0;
        const HRESULT hr = p_->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dp, nullptr, &ex, &argErr);
        if (FAILED(hr))
            throw ComError(describe(name, hr, ex), hr);
    }

private:
    DISPID lookup(const wchar_t* name) const
    {
        if (!p_)
            throw ComError("objeto COM nulo", E_POINTER);
        DISPID id = 0;
        LPOLESTR n = const_cast<LPOLESTR>(name);
        const HRESULT hr = p_->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
        if (FAILED(hr))
            throw ComError("membro COM desconhecido: " + toUtf8(name), hr);
        return id;
    }

    static std::string describe(const wchar_t* name, HRESULT hr, const EXCEPINFO& ex)
    {
        std::string msg = toUtf8(name) + " falhou (" + hrText(hr) + ")";
        if (ex.bstrDescription) {
            msg += ": " + toUtf8(std::wstring(ex.bstrDescription, SysStringLen(ex.bstrDescription)));
        }
        return msg;
    }

    Var invoke(WORD flags, const wchar_t* name, std::initializer_list<const Var*> args) const
    {
        const DISPID dispid = lookup(name);
        const size_t n = args.size();
        VARIANT stackArgs[12];
        if (n > 12)
            throw ComError("argumentos demais", E_INVALIDARG);
        size_t i = 0;
        for (const Var* a : args)
            stackArgs[n - 1 - i++] = *a->get();
        DISPPARAMS dp{};
        dp.cArgs = static_cast<UINT>(n);
        dp.rgvarg = n ? stackArgs : nullptr;
        Var result;
        EXCEPINFO ex{};
        UINT argErr = 0;
        const HRESULT hr = p_->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result.get(), &ex, &argErr);
        if (FAILED(hr))
            throw ComError(describe(name, hr, ex), hr);
        return result;
    }

    IDispatch* p_ = nullptr;
};

struct AppInstance
{
    Object app;
    bool createdByUs = false;
};

inline AppInstance attachOrCreate(const wchar_t* progId, std::string* error)
{
    AppInstance out;
    CLSID clsid{};
    HRESULT hr = CLSIDFromProgID(progId, &clsid);
    if (FAILED(hr)) {
        if (error)
            *error = std::string(toUtf8(progId)) + " nao registrado (" + hrText(hr) + ")";
        return out;
    }
    IUnknown* unk = nullptr;
    if (SUCCEEDED(GetActiveObject(clsid, nullptr, &unk)) && unk) {
        IDispatch* disp = nullptr;
        if (SUCCEEDED(unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&disp))) && disp) {
            out.app = Object(disp);
            out.createdByUs = false;
        }
        unk->Release();
        if (out.app)
            return out;
    }
    IDispatch* disp = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&disp));
    if (FAILED(hr) || !disp) {
        if (error)
            *error = "nao foi possivel iniciar " + toUtf8(progId) + " (" + hrText(hr) + ")";
        return out;
    }
    out.app = Object(disp);
    out.createdByUs = true;
    return out;
}

}
