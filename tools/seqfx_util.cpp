#include "seqfx.h"

#include "fx/bsl/SequenceFeed.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stb_image_write.h>

namespace seqfx {

using namespace bsl;
using namespace bsl::fx::bsllight;

bool perto(int a, int b, int tol)
{
    return std::abs(a - b) <= tol;
}

bool corIgual(const Pixel& p, int r, int g, int b, int a, int tol)
{
    return perto(p.r, r, tol) && perto(p.g, g, tol) && perto(p.b, b, tol) && perto(p.a, a, tol);
}

GLuint criarTextura(int w, int h, const std::vector<unsigned char>& dados)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, dados.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return t;
}

std::vector<unsigned char> imagemUniforme(int w, int h, int r, int g, int b)
{
    std::vector<unsigned char> d(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (size_t i = 0; i < d.size(); i += 4) {
        d[i] = static_cast<unsigned char>(r);
        d[i + 1] = static_cast<unsigned char>(g);
        d[i + 2] = static_cast<unsigned char>(b);
        d[i + 3] = 255;
    }
    return d;
}

std::vector<unsigned char> imagemMetades(int w, int h)
{
    std::vector<unsigned char> d(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = d.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            const bool esquerda = x < w / 2;
            p[0] = esquerda ? 255 : 0;
            p[1] = 0;
            p[2] = esquerda ? 0 : 255;
            p[3] = 255;
        }
    return d;
}

Pixel ler(const Fbo& fbo, int x, int linha)
{
    unsigned char px[4] = {0, 0, 0, 0};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.id());
    glReadPixels(x, linha, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return {px[0], px[1], px[2], px[3]};
}

Pixel lerTextura(GLuint textura, int x, int linha)
{
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textura, 0);
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(x, linha, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return {px[0], px[1], px[2], px[3]};
}

std::vector<unsigned char> lerTudo(const Fbo& fbo)
{
    std::vector<unsigned char> d(static_cast<size_t>(fbo.width()) * static_cast<size_t>(fbo.height()) * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbo.width(), fbo.height(), GL_RGBA, GL_UNSIGNED_BYTE, d.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return d;
}

void salvarPng(const Fbo& fbo, const std::string& nome)
{
    const std::filesystem::path pasta("docs");
    std::error_code ec;
    if (!std::filesystem::is_directory(pasta, ec))
        return;
    const std::vector<unsigned char> d = lerTudo(fbo);
    const std::string caminho = (pasta / ("screenshot-seq-" + nome + ".png")).string();
    stbi_write_png(caminho.c_str(), fbo.width(), fbo.height(), 4, d.data(), fbo.width() * 4);
}

std::vector<unsigned char> imagemGradiente(int w, int h)
{
    std::vector<unsigned char> d(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = d.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            p[0] = static_cast<unsigned char>(40 + 200.0f * fx);
            p[1] = static_cast<unsigned char>(40 + 160.0f * fy);
            p[2] = static_cast<unsigned char>(240.0f - 180.0f * fx * fy);
            p[3] = 255;
        }
    return d;
}

std::vector<unsigned char> imagemVertical(int w, int h)
{
    std::vector<unsigned char> d(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = d.data() + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            const bool cima = y < h / 2;
            p[0] = cima ? 255 : 0;
            p[1] = 0;
            p[2] = cima ? 0 : 255;
            p[3] = 255;
        }
    return d;
}

std::vector<FeedSlice> grade(int colunas, int linhas, int largura, int altura, int folga)
{
    std::vector<FeedSlice> v;
    const float l = static_cast<float>(largura) / static_cast<float>(colunas);
    const float a = static_cast<float>(altura) / static_cast<float>(linhas);
    int uid = 1;
    for (int j = 0; j < linhas; ++j)
        for (int i = 0; i < colunas; ++i)
            v.push_back(criarSlice(uid++, l * static_cast<float>(i), a * static_cast<float>(j),
                                   l - static_cast<float>(folga), a - static_cast<float>(folga)));
    return v;
}

int diferentes(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b)
{
    if (a.size() != b.size())
        return static_cast<int>(std::max(a.size(), b.size()));
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            ++n;
    return n;
}

FeedSlice criarSlice(int uid, float x, float y, float w, float h)
{
    FeedSlice s;
    s.uid = uid;
    s.x = x;
    s.y = y;
    s.w = w;
    s.h = h;
    return s;
}

std::vector<FeedSlice> colunas(int quantidade, int largura, int altura)
{
    std::vector<FeedSlice> v;
    const float l = static_cast<float>(largura) / static_cast<float>(quantidade);
    for (int i = 0; i < quantidade; ++i)
        v.push_back(criarSlice(i + 1, l * static_cast<float>(i), 0.0f, l, static_cast<float>(altura)));
    return v;
}

void publicarSlices(const std::vector<FeedSlice>& slices, int canvasW, int canvasH)
{
    SliceFeed::instance().set(slices, canvasW, canvasH);
}

void publicarConjunto(const seq::SequenceSet& conjunto)
{
    SequenceFeed::instance().set(conjunto, {});
}

}
