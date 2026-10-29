// glewInit() dos plugins de teste: carrega o OpenGL pelo glad.
#include <GL/glew.h>

#include <windows.h>

static HMODULE g_opengl32;

static GLADapiproc loadProc(const char* name)
{
    GLADapiproc p = (GLADapiproc)wglGetProcAddress(name);
    if (!p || p == (GLADapiproc)(INT_PTR)1 || p == (GLADapiproc)(INT_PTR)2 || p == (GLADapiproc)(INT_PTR)3
        || p == (GLADapiproc)(INT_PTR)-1) {
        if (!g_opengl32)
            g_opengl32 = LoadLibraryA("opengl32.dll");
        p = (GLADapiproc)GetProcAddress(g_opengl32, name);
    }
    return p;
}

int glewInit(void)
{
    return gladLoadGL(loadProc) ? GLEW_OK : 1;
}
