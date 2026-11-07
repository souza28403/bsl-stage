#include <windows.h>

#if defined(FAIL_NOMAIN)

extern "C" __declspec(dllexport) int __stdcall SomethingElse(int x)
{
    return x + 1;
}

#else

#include <FFGL.h>

#include <cstring>

namespace {

PluginInfoStruct g_info = {2, 1, {'F', 'A', 'I', 'L'}, {'F', 'a', 'i', 'l', ' ', 't', 'e', 's', 't', ' ', ' ', ' ', ' ', ' ', ' ', ' '},
                           FF_EFFECT};
PluginExtendedInfoStruct g_ext = {1, 0, "Plugin de teste que falha", "BSL Stage (teste)", 0, nullptr};
char g_instance[16];

}

extern "C" __declspec(dllexport) FFMixed __stdcall plugMain(FFUInt32 code, FFMixed in, FFInstanceID instance)
{
    (void)in;
    (void)instance;
    FFMixed out;
    out.PointerValue = nullptr;
    out.UIntValue = FF_SUCCESS;
    switch (code) {
    case FF_GET_INFO:
#if defined(FAIL_HANG)
        for (;;)
            Sleep(1000);
#endif
        out.PointerValue = &g_info;
        break;
    case FF_GET_EXTENDED_INFO:
        out.PointerValue = &g_ext;
        break;
    case FF_INITIALISE_V2:
        out.UIntValue = FF_SUCCESS;
        break;
    case FF_GET_NUM_PARAMETERS:
        out.UIntValue = 0;
        break;
    case FF_GET_PLUGIN_CAPS:
        out.UIntValue = (in.UIntValue == FF_CAP_MINIMUM_INPUT_FRAMES || in.UIntValue == FF_CAP_MAXIMUM_INPUT_FRAMES) ? 1 : FF_FALSE;
        break;
    case FF_INSTANTIATE_GL:
        out.PointerValue = g_instance;
        break;
    case FF_PROCESS_OPENGL:
#if defined(FAIL_CRASH)
        *static_cast<volatile int*>(nullptr) = 42;
#endif
        out.UIntValue = FF_SUCCESS;
        break;
    default:
        out.UIntValue = FF_FAIL;
        break;
    }
    return out;
}

#endif
