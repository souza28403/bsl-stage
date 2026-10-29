// Shim do BSL Stage: o FFGL.h original inclui <GL/glew.h>. O host nao usa o GLEW (so os tipos do GL); aqui os tipos
// vem do glad, que o app ja usa. Os plugins (DLLs) trazem o proprio GLEW e nao passam por este arquivo.
#pragma once
#include <glad/gl.h>
