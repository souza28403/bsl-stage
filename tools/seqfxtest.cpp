#include "seqfx.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace seqfx {

int falhas = 0;
int verificacoes = 0;

}

int main()
{
    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* janela = glfwCreateWindow(64, 64, "seqfxtest", nullptr, nullptr);
    if (!janela) {
        glfwTerminate();
        return 2;
    }
    glfwMakeContextCurrent(janela);
    if (!gladLoadGL(glfwGetProcAddress)) {
        glfwDestroyWindow(janela);
        glfwTerminate();
        return 2;
    }
    seqfx::testarBase();
    seqfx::testarInstanciador();
    seqfx::testarSequencia();
    seqfx::testarDisparo();
    seqfx::testarAtraso();
    seqfx::testarAleatorios();
    seqfx::testarFonte();
    seqfx::testarContorno();
    seqfx::testarLadrilhos();
    seqfx::testarAmostrador();
    seqfx::testarVarredura();
    seqfx::testarBordaNeon();
    glfwDestroyWindow(janela);
    glfwTerminate();
    std::printf("%d verificacoes, %d falhas\n", seqfx::verificacoes, seqfx::falhas);
    return seqfx::falhas == 0 ? 0 : 1;
}
