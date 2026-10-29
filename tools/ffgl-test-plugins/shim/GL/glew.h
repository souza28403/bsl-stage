// Shim de teste: o FFGL.h do SDK inclui <GL/glew.h>. Nos plugins de teste os tipos e as funcoes do GL vem do glad e o
// glewInit() carrega os ponteiros com wglGetProcAddress (ver glew_shim.c). Nao e o GLEW de verdade.
#pragma once
#include <glad/gl.h>

#define GLEW_OK 0

#ifdef __cplusplus
extern "C" {
#endif
int glewInit(void);
#ifdef __cplusplus
}
#endif
