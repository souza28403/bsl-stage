#include "seqfx.h"

#include <glad/gl.h>

namespace seqfx {

void testarBase()
{
    secao("base");
    VERIFICAR(glGetError() == GL_NO_ERROR);
}

}
