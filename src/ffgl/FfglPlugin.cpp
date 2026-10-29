#include <windows.h>

#include "ffgl/FfglPlugin.h"

#include "ffgl/FfglCall.h"

#include <FFGL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bsl::ffgl {

// Os codigos de ffgl::fn tem que bater com o cabecalho do SDK
static_assert(fn::GetInfo == FF_GET_INFO && fn::InitialiseV2 == FF_INITIALISE_V2 && fn::Deinitialise == FF_DEINITIALISE
              && fn::GetNumParameters == FF_GET_NUM_PARAMETERS && fn::GetParameterName == FF_GET_PARAMETER_NAME
              && fn::GetParameterDefault == FF_GET_PARAMETER_DEFAULT && fn::GetParameterDisplay == FF_GET_PARAMETER_DISPLAY
              && fn::SetParameter == FF_SET_PARAMETER && fn::GetParameter == FF_GET_PARAMETER
              && fn::GetPluginCaps == FF_GET_PLUGIN_CAPS && fn::EnablePluginCap == FF_ENABLE_PLUGIN_CAP
              && fn::GetExtendedInfo == FF_GET_EXTENDED_INFO && fn::GetParameterType == FF_GET_PARAMETER_TYPE
              && fn::ProcessOpenGL == FF_PROCESS_OPENGL && fn::InstantiateGL == FF_INSTANTIATE_GL
              && fn::DeinstantiateGL == FF_DEINSTANTIATE_GL && fn::SetTime == FF_SET_TIME && fn::Resize == FF_RESIZE
              && fn::GetNumParameterElements == FF_GET_NUM_PARAMETER_ELEMENTS
              && fn::GetParameterElementName == FF_GET_PARAMETER_ELEMENT_NAME
              && fn::GetParameterElementValue == FF_GET_PARAMETER_ELEMENT_VALUE
              && fn::GetParameterUsage == FF_GET_PARAMETER_USAGE && fn::SetBeatInfo == FF_SET_BEATINFO
              && fn::SetHostInfo == FF_SET_HOSTINFO && fn::GetRange == FF_GET_RANGE
              && fn::GetParamGroup == FF_GET_PARAM_GROUP && fn::GetNumFileExtensions == FF_GET_NUM_FILE_PARAMETER_EXTENSIONS
              && fn::GetFileExtension == FF_GET_FILE_PARAMETER_EXTENSION && fn::GetParamVisibility == FF_GET_PRAMETER_VISIBILITY
              && fn::GetParamEvents == FF_GET_PARAMETER_EVENTS,
              "codigos de funcao do FFGL");
static_assert(sizeof(FFMixed) == sizeof(Word), "FFMixed deve ter o tamanho de um ponteiro");

float CallResult::f32() const
{
    const std::uint32_t bits = u32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

Word floatWord(float v)
{
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return static_cast<Word>(bits);
}

std::string exceptionText(unsigned long code)
{
    const char* what = "exceção estruturada";
    switch (code) {
    case 0xC0000005: what = "violação de acesso"; break;
    case 0xC00000FD: what = "estouro de pilha"; break;
    case 0xC0000094: what = "divisão inteira por zero"; break;
    case 0xC000001D: what = "instrução ilegal"; break;
    case 0xC0000096: what = "instrução privilegiada"; break;
    case 0xC0000409: what = "falha de segurança da pilha"; break;
    case 0xE06D7363: what = "exceção C++ não tratada"; break;
    case 0x80000003: what = "breakpoint"; break;
    default: break;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "0x%08lX (%s)", code, what);
    return buf;
}

// ----------------------------------------------------------------------------------------------- Plugin

namespace {

Plugin::LogSink g_logSink = nullptr;

void __stdcall pluginLogTrampoline(char* text)
{
    if (!g_logSink || !text)
        return;
    char buf[512];
    unsigned long exc = 0;
    if (!rawCString(text, buf, sizeof(buf), &exc))
        return;
    std::string msg(buf);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' '))
        msg.pop_back();
    if (!msg.empty())
        g_logSink(msg);
}

} // namespace

void Plugin::setLogSink(LogSink sink)
{
    g_logSink = sink;
}

std::shared_ptr<Plugin> Plugin::open(const std::filesystem::path& file, std::string* error)
{
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(file, ec);
    HMODULE module = LoadLibraryExW(abs.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        if (error)
            *error = "não foi possível carregar a DLL (erro " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    void* mainFn = reinterpret_cast<void*>(GetProcAddress(module, "plugMain"));
    if (!mainFn) {
        FreeLibrary(module); // o filtro de PE ja deveria ter barrado
        if (error)
            *error = "a DLL não exporta plugMain";
        return nullptr;
    }
    if (g_logSink) { // SetLogCallback e opcional
        if (void* setLog = reinterpret_cast<void*>(GetProcAddress(module, "SetLogCallback"))) {
            unsigned long exc = 0;
            rawSetLogCallback(setLog, reinterpret_cast<void*>(&pluginLogTrampoline), &exc);
        }
    }
    std::shared_ptr<Plugin> p(new Plugin());
    p->path_ = abs;
    p->module_ = module;
    p->plugMain_ = mainFn;
    return p;
}

Plugin::~Plugin() = default;

CallResult Plugin::call(std::uint32_t code, Word in, void* instance)
{
    CallResult r;
    r.ok = rawPlugMain(plugMain_, code, in, instance, &r.value, &r.exception);
    return r;
}

bool Plugin::initialise(std::string* error)
{
    if (initialised_)
        return true;
    const CallResult r = call(fn::InitialiseV2);
    if (!r.ok) {
        if (error)
            *error = "exceção " + exceptionText(r.exception) + " em FF_INITIALISE_V2";
        return false;
    }
    if (r.u32() != FF_SUCCESS) {
        if (error)
            *error = "FF_INITIALISE_V2 recusado pelo plugin";
        return false;
    }
    initialised_ = true;
    return true;
}

void Plugin::deinitialise()
{
    if (!initialised_)
        return;
    initialised_ = false;
    call(fn::Deinitialise);
}

void Plugin::unload()
{
    if (module_) {
        FreeLibrary(static_cast<HMODULE>(module_));
        module_ = nullptr;
        plugMain_ = nullptr;
    }
}

bool Plugin::readMemory(Word ptr, void* dst, std::size_t n)
{
    if (ptr == 0 || ptr == 0xFFFFFFFFull)
        return false;
    unsigned long exc = 0;
    return rawCopy(reinterpret_cast<const void*>(ptr), dst, n, &exc);
}

std::string Plugin::readString(Word ptr, std::size_t maxLen)
{
    if (ptr == 0 || ptr == 0xFFFFFFFFull) // nulo ou (char*)FF_FAIL
        return {};
    std::string buf(maxLen + 1, '\0');
    unsigned long exc = 0;
    rawCString(reinterpret_cast<const char*>(ptr), buf.data(), buf.size(), &exc);
    buf.resize(std::strlen(buf.c_str()));
    return buf;
}

// ----------------------------------------------------------------------------------------------- varredura

namespace {

// UTF-8 valido sempre (o JSON nao aceita bytes soltos); o que nao for ASCII/UTF-8 vira '?'
std::string cleanText(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        size_t len = 1;
        if (c >= 0xF0 && c <= 0xF4) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC2) len = 2;
        if (c < 0x80) {
            out += (c >= 0x20 || c == '\n' || c == '\t') ? static_cast<char>(c) : ' ';
            ++i;
            continue;
        }
        bool valid = len > 1 && i + len <= in.size();
        for (size_t k = 1; valid && k < len; ++k)
            valid = (static_cast<unsigned char>(in[i + k]) & 0xC0) == 0x80;
        if (valid) {
            out.append(in, i, len);
            i += len;
        } else {
            out += '?';
            ++i;
        }
    }
    return out;
}

std::string trimRight(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0' || s.back() == '\t'))
        s.pop_back();
    return s;
}

std::string utf8Path(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

} // namespace

ScanResult scanPluginFile(const std::filesystem::path& file, std::shared_ptr<Plugin>* loaded)
{
    ScanResult res;
    auto fail = [&](const std::string& msg) {
        res.ok = false;
        res.error = msg;
        return res;
    };
    auto failCall = [&](const char* what, const CallResult& r) {
        return fail("exceção " + exceptionText(r.exception) + " em " + what);
    };

    std::string error;
    std::shared_ptr<Plugin> plugin = Plugin::open(file, &error);
    if (!plugin)
        return fail(error);
    if (loaded)
        *loaded = plugin;

    PluginInfo& info = res.info;
    info.path = utf8Path(plugin->path());
    info.fileName = utf8Path(plugin->path().filename());
    {
        WIN32_FILE_ATTRIBUTE_DATA attr{};
        if (GetFileAttributesExW(plugin->path().c_str(), GetFileExInfoStandard, &attr)) {
            info.size = (static_cast<std::uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
            info.mtime = static_cast<std::int64_t>((static_cast<std::uint64_t>(attr.ftLastWriteTime.dwHighDateTime) << 32)
                                                   | attr.ftLastWriteTime.dwLowDateTime);
        }
    }

    // --- FF_GET_INFO ---
    const CallResult gi = plugin->call(fn::GetInfo);
    if (!gi.ok)
        return failCall("FF_GET_INFO", gi);
    PluginInfoStruct pis{};
    if (!plugin->readMemory(gi.value, &pis, sizeof(pis)))
        return fail("FF_GET_INFO devolveu um ponteiro inválido");
    info.apiMajor = pis.APIMajorVersion;
    info.apiMinor = pis.APIMinorVersion;
    info.pluginId = cleanText(std::string(pis.PluginUniqueID, 4));
    info.name = cleanText(trimRight(std::string(pis.PluginName, strnlen(pis.PluginName, sizeof(pis.PluginName)))));
    info.type = pis.PluginType;
    if (info.apiMajor < 2)
        return fail("plugin FFGL " + std::to_string(info.apiMajor) + "." + std::to_string(info.apiMinor)
                    + " (só FFGL 2.x é suportado)");

    // --- FF_GET_EXTENDED_INFO (opcional) ---
    const CallResult ei = plugin->call(fn::GetExtendedInfo);
    if (!ei.ok)
        return failCall("FF_GET_EXTENDED_INFO", ei);
    PluginExtendedInfoStruct pei{};
    if (plugin->readMemory(ei.value, &pei, sizeof(pei))) {
        info.versionMajor = pei.PluginMajorVersion;
        info.versionMinor = pei.PluginMinorVersion;
        info.description = cleanText(plugin->readString(reinterpret_cast<Word>(pei.Description), 2048));
        info.about = cleanText(plugin->readString(reinterpret_cast<Word>(pei.About), 2048));
    }

    // --- FF_INITIALISE_V2 ---
    if (!plugin->initialise(&error))
        return fail(error);

    // --- capacidades ---
    {
        const CallResult c = plugin->call(fn::GetPluginCaps, FF_CAP_SET_TIME);
        if (!c.ok)
            return failCall("FF_GET_PLUGIN_CAPS", c);
        info.supportsTime = c.u32() == FF_TRUE;
        const CallResult tl = plugin->call(fn::GetPluginCaps, FF_CAP_TOP_LEFT_TEXTURE_ORIENTATION);
        info.supportsTopLeft = tl.ok && tl.u32() == FF_TRUE;
        const CallResult mn = plugin->call(fn::GetPluginCaps, FF_CAP_MINIMUM_INPUT_FRAMES);
        const CallResult mx = plugin->call(fn::GetPluginCaps, FF_CAP_MAXIMUM_INPUT_FRAMES);
        info.minInputs = (mn.ok && mn.u32() != FF_FAIL && mn.u32() < 64) ? static_cast<int>(mn.u32()) : -1;
        info.maxInputs = (mx.ok && mx.u32() != FF_FAIL && mx.u32() < 64) ? static_cast<int>(mx.u32()) : -1;
    }

    // --- parametros ---
    const CallResult np = plugin->call(fn::GetNumParameters);
    if (!np.ok)
        return failCall("FF_GET_NUM_PARAMETERS", np);
    const std::uint32_t count = (np.u32() == FF_FAIL) ? 0 : std::min<std::uint32_t>(np.u32(), 512);
    for (std::uint32_t i = 0; i < count; ++i) {
        ParamInfo p;
        p.index = static_cast<int>(i);

        const CallResult nm = plugin->call(fn::GetParameterName, i);
        if (!nm.ok)
            return failCall("FF_GET_PARAMETER_NAME", nm);
        p.name = cleanText(trimRight(plugin->readString(nm.value, 128)));
        if (p.name.empty())
            p.name = "Parâmetro " + std::to_string(i + 1);

        const CallResult ty = plugin->call(fn::GetParameterType, i);
        if (!ty.ok)
            return failCall("FF_GET_PARAMETER_TYPE", ty);
        p.type = ty.u32();

        const CallResult df = plugin->call(fn::GetParameterDefault, i);
        if (!df.ok)
            return failCall("FF_GET_PARAMETER_DEFAULT", df);
        if (p.type == FF_TYPE_TEXT || p.type == FF_TYPE_FILE)
            p.defText = cleanText(plugin->readString(df.value, 4096));
        else if (df.u32() != FF_FAIL)
            p.defFloat = df.f32();

        GetRangeStruct range{};
        range.parameterNumber = i;
        range.range = {0.0f, 1.0f};
        const CallResult rg = plugin->callPtr(fn::GetRange, &range);
        if (!rg.ok)
            return failCall("FF_GET_RANGE", rg);
        if (rg.u32() == FF_SUCCESS && std::isfinite(range.range.min) && std::isfinite(range.range.max)
            && range.range.max > range.range.min) {
            p.min = range.range.min;
            p.max = range.range.max;
        }

        char groupBuf[257] = {};
        GetStringStruct gs{};
        gs.parameterNumber = i;
        gs.stringBuffer.address = groupBuf;
        gs.stringBuffer.maxToWrite = 256;
        const CallResult gr = plugin->callPtr(fn::GetParamGroup, &gs);
        if (!gr.ok)
            return failCall("FF_GET_PARAM_GROUP", gr);
        if (gr.u32() == FF_SUCCESS)
            p.group = cleanText(trimRight(groupBuf));

        const CallResult us = plugin->call(fn::GetParameterUsage, i);
        p.usage = (us.ok && us.u32() != FF_FAIL) ? us.u32() : 0;
        const CallResult vis = plugin->call(fn::GetParamVisibility, i);
        p.visible = !vis.ok || vis.u32() != FF_FALSE;

        if (p.type == FF_TYPE_OPTION) {
            const CallResult ne = plugin->call(fn::GetNumParameterElements, i);
            if (!ne.ok)
                return failCall("FF_GET_NUM_PARAMETER_ELEMENTS", ne);
            const std::uint32_t n = ne.u32() == FF_FAIL ? 0 : std::min<std::uint32_t>(ne.u32(), 512);
            for (std::uint32_t e = 0; e < n; ++e) {
                GetParameterElementNameStruct an{i, e};
                GetParameterElementValueStruct av{i, e};
                const CallResult en = plugin->callPtr(fn::GetParameterElementName, &an);
                if (!en.ok)
                    return failCall("FF_GET_PARAMETER_ELEMENT_NAME", en);
                const CallResult ev = plugin->callPtr(fn::GetParameterElementValue, &av);
                if (!ev.ok)
                    return failCall("FF_GET_PARAMETER_ELEMENT_VALUE", ev);
                OptionInfo o;
                o.name = cleanText(plugin->readString(en.value, 128));
                if (o.name.empty())
                    o.name = "Opção " + std::to_string(e + 1);
                o.value = ev.f32();
                p.options.push_back(std::move(o));
            }
        } else if (p.type == FF_TYPE_FILE) {
            const CallResult ne = plugin->call(fn::GetNumFileExtensions, i);
            if (!ne.ok)
                return failCall("FF_GET_NUM_FILE_PARAMETER_EXTENSIONS", ne);
            const std::uint32_t n = ne.u32() == FF_FAIL ? 0 : std::min<std::uint32_t>(ne.u32(), 128);
            for (std::uint32_t e = 0; e < n; ++e) {
                GetFileParameterExtensionStruct ax{i, e};
                const CallResult ex = plugin->callPtr(fn::GetFileExtension, &ax);
                if (!ex.ok)
                    return failCall("FF_GET_FILE_PARAMETER_EXTENSION", ex);
                std::string ext = cleanText(plugin->readString(ex.value, 32));
                while (!ext.empty() && (ext.front() == '.' || ext.front() == '*'))
                    ext.erase(ext.begin());
                if (!ext.empty())
                    p.extensions.push_back(std::move(ext));
            }
        }
        info.params.push_back(std::move(p));
    }

    // FF_DEINITIALISE e o descarregamento ficam para o chamador, depois de gravar o resultado (o plugin pode travar ali)
    res.ok = true;
    return res;
}

} // namespace bsl::ffgl
