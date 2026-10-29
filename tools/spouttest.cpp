// bslstage_spouttest: cria um sender Spout (bsl::SpoutOutput), recebe com um receiver no mesmo
// processo e confere os pixels. Precisa de GPU/driver com OpenGL e D3D11 (janela GLFW oculta).
// Código de saída 0 = passou.

#include "output/SpoutOutput.h"
#include "spouttest_source.h"

#include <windows.h>

#pragma warning(push, 0)
#include <SpoutReceiver.h>
#pragma warning(pop)

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr int kW = 64;
constexpr int kH = 32;

struct Px { int r, g, b; };

// Quadrantes (em linhas de baixo para cima, como o GL guarda): embaixo-esq vermelho, embaixo-dir verde,
// em cima-esq azul, em cima-dir amarelo.
Px quadrantColor(int x, int y)
{
    const bool right = x >= kW / 2;
    const bool top = y >= kH / 2; // y cresce para cima no buffer GL
    if (!top)
        return right ? Px{0, 255, 0} : Px{255, 0, 0};
    return right ? Px{255, 255, 0} : Px{0, 0, 255};
}

bool closeTo(Px a, Px b)
{
    return std::abs(a.r - b.r) <= 3 && std::abs(a.g - b.g) <= 3 && std::abs(a.b - b.b) <= 3;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        std::printf("FAIL: glfwInit\n");
        return 2;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(256, 256, "spouttest", nullptr, nullptr);
    if (!window) {
        std::printf("FAIL: janela GL\n");
        return 2;
    }
    glfwMakeContextCurrent(window);

    // Textura de origem com os quadrantes
    std::vector<unsigned char> src(static_cast<size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const Px c = quadrantColor(x, y);
            unsigned char* p = &src[(static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4];
            p[0] = static_cast<unsigned char>(c.r);
            p[1] = static_cast<unsigned char>(c.g);
            p[2] = static_cast<unsigned char>(c.b);
            p[3] = 255;
        }
    GLuint srcTex = 0, rxTex = 0;
    glGenTextures(1, &srcTex);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
    glGenTextures(1, &rxTex);

    bsl::SpoutOutput out;
    out.setName("BSLStageSpoutTest");
    SpoutReceiver rx;
    rx.SetReceiverName("BSLStageSpoutTest");

    bool received = false;
    int rw = 0, rh = 0;
    for (int i = 0; i < 200 && !received; ++i) {
        if (!out.send(srcTex, kW, kH)) {
            std::printf("FAIL: SpoutOutput::send devolveu false (iteracao %d)\n", i);
            return 1;
        }
        if (rx.IsUpdated()) { // tamanho do sender mudou / primeira conexao
            rw = static_cast<int>(rx.GetSenderWidth());
            rh = static_cast<int>(rx.GetSenderHeight());
            glBindTexture(GL_TEXTURE_2D, rxTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }
        // O sender inverte (textura GL origem embaixo -> compartilhada origem em cima, como nos outros
        // apps de VJ); um receiver GL inverte de volta com bInvert = true.
        if (rx.ReceiveTexture(rxTex, GL_TEXTURE_2D, true) && rx.IsConnected() && rw == kW && rh == kH && !rx.IsUpdated())
            received = rx.IsFrameNew() || i > 5;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!received) {
        std::printf("FAIL: o receiver nao recebeu quadro (conectado=%d, %dx%d)\n", rx.IsConnected() ? 1 : 0, rw, rh);
        return 1;
    }

    std::vector<unsigned char> dst(static_cast<size_t>(rw) * static_cast<size_t>(rh) * 4);
    glBindTexture(GL_TEXTURE_2D, rxTex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, dst.data());

    auto pixel = [&](int x, int y) {
        const unsigned char* p = &dst[(static_cast<size_t>(y) * static_cast<size_t>(rw) + static_cast<size_t>(x)) * 4];
        return Px{p[0], p[1], p[2]};
    };

    int failures = 0;
    const int xs[2] = {kW / 4, 3 * kW / 4};
    const int ys[2] = {kH / 4, 3 * kH / 4};
    for (int yi = 0; yi < 2; ++yi)
        for (int xi = 0; xi < 2; ++xi) {
            const Px want = quadrantColor(xs[xi], ys[yi]);
            const Px got = pixel(xs[xi], ys[yi]);
            const bool ok = closeTo(got, want);
            std::printf("pixel (%2d,%2d) %s  got=(%d,%d,%d) want=(%d,%d,%d)\n", xs[xi], ys[yi], ok ? "OK  " : "FAIL",
                        got.r, got.g, got.b, want.r, want.g, want.b);
            failures += ok ? 0 : 1;
        }

    rx.ReleaseReceiver();

    // Segunda parte: o mesmo sender visto pela fonte Spout do app (gerador "gen:spout", entrada de qualquer programa DX/GL).
    // A textura da fonte sai com a linha 0 = topo da imagem: o topo do GL (y >= kH/2 no buffer do sender) vem primeiro.
    {
        spouttest::AppSourceResult app;
        const bool ran = spouttest::receiveWithAppSource("BSLStageSpoutTest", [&] { out.send(srcTex, kW, kH); }, app);
        const bool setup = ran && app.registered && app.isLive;
        std::printf("fonte Spout do app: registrada=%d ao_vivo=%d na_lista=%d conectou=%d %dx%d status=\"%s\"\n",
                    app.registered ? 1 : 0, app.isLive ? 1 : 0, app.listed ? 1 : 0, app.connected ? 1 : 0, app.width,
                    app.height, app.status.c_str());
        if (!setup || !app.listed || !app.connected || app.width != kW || app.height != kH) {
            std::printf("FAIL: a fonte Spout do app nao recebeu o sender\n");
            ++failures;
        } else {
            const int axs[2] = {kW / 4, 3 * kW / 4};
            const int ays[2] = {kH / 4, 3 * kH / 4};
            for (int yi = 0; yi < 2; ++yi)
                for (int xi = 0; xi < 2; ++xi) {
                    // y da memoria cresce para baixo na imagem; o quadrante de origem tem y GL = kH - 1 - y
                    const Px want = quadrantColor(axs[xi], kH - 1 - ays[yi]);
                    const unsigned char* p = &app.rgba[(static_cast<size_t>(ays[yi]) * kW + static_cast<size_t>(axs[xi])) * 4];
                    const Px got{p[0], p[1], p[2]};
                    const bool ok = closeTo(got, want);
                    std::printf("fonte pixel (%2d,%2d) %s  got=(%d,%d,%d) want=(%d,%d,%d)\n", axs[xi], ays[yi],
                                ok ? "OK  " : "FAIL", got.r, got.g, got.b, want.r, want.g, want.b);
                    failures += ok ? 0 : 1;
                }
        }
    }

    out.release();
    glDeleteTextures(1, &srcTex);
    glDeleteTextures(1, &rxTex);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf(failures == 0 ? "Spout: OK (%dx%d)\n" : "Spout: FALHOU (%dx%d)\n", rw, rh);
    return failures == 0 ? 0 : 1;
}
