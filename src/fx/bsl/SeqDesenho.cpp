#include "fx/bsl/SeqDesenho.h"

#include <algorithm>
#include <stdexcept>

namespace bsl::fx::bsllight {

const char* const kVerticeSlices = R"(#version 330 core
layout(location = 0) in vec2 aCanto;
layout(location = 1) in vec4 aRet;
layout(location = 2) in vec2 aInt;
layout(location = 3) in vec4 aExtra;
uniform int uTop;
uniform vec2 uMargem;
out vec2 vLocal;
out vec2 vImg;
flat out vec4 vRet;
flat out float vInt;
flat out float vIdx;
flat out vec4 vExtra;
void main()
{
    vec2 img = aRet.xy + aCanto * aRet.zw + (aCanto * 2.0 - 1.0) * uMargem;
    vLocal = (img - aRet.xy) / max(aRet.zw, vec2(1.0e-6));
    vImg = img;
    vRet = aRet;
    vInt = aInt.x;
    vIdx = aInt.y;
    vExtra = aExtra;
    vec2 uv = uTop == 1 ? img : vec2(img.x, 1.0 - img.y);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

namespace {
constexpr int kFloatsPorInstancia = 10;
}

DesenhoSlices::DesenhoSlices(const char* fragmento, const char* vertice) : vertice_(vertice), fragmento_(fragmento) {}

DesenhoSlices::~DesenhoSlices()
{
    liberar();
}

void DesenhoSlices::liberar()
{
    if (dados_)
        glDeleteBuffers(1, &dados_);
    if (cantos_)
        glDeleteBuffers(1, &cantos_);
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    dados_ = cantos_ = vao_ = 0;
    shader_.reset();
}

void DesenhoSlices::criar()
{
    tentou_ = true;
    try {
        shader_ = std::make_unique<Shader>(vertice_, fragmento_);
    } catch (const std::exception& e) {
        erro_ = std::string("Falha no shader do efeito: ") + e.what();
        shader_.reset();
        return;
    }
    const float cantos[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &cantos_);
    glGenBuffers(1, &dados_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, cantos_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cantos), cantos, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, dados_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(float)) * kFloatsPorInstancia, nullptr, GL_STREAM_DRAW);
    const GLsizei passo = static_cast<GLsizei>(sizeof(float) * kFloatsPorInstancia);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(sizeof(float) * 4));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, passo, reinterpret_cast<const void*>(sizeof(float) * 6));
    glVertexAttribDivisor(3, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool DesenhoSlices::pronto()
{
    if (!tentou_)
        criar();
    return shader_ != nullptr;
}

void DesenhoSlices::carregar(const std::vector<InstanciaSlice>& instancias)
{
    total_ = static_cast<int>(std::min<size_t>(instancias.size(), static_cast<size_t>(kMaxInstancias)));
    if (total_ <= 0 || !pronto())
        return;
    buffer_.resize(static_cast<size_t>(total_) * kFloatsPorInstancia);
    for (int i = 0; i < total_; ++i) {
        const InstanciaSlice& s = instancias[static_cast<size_t>(i)];
        float* p = buffer_.data() + static_cast<size_t>(i) * kFloatsPorInstancia;
        p[0] = s.x;
        p[1] = s.y;
        p[2] = s.largura;
        p[3] = s.altura;
        p[4] = s.intensidade;
        p[5] = s.indice;
        p[6] = s.extra[0];
        p[7] = s.extra[1];
        p[8] = s.extra[2];
        p[9] = s.extra[3];
    }
    glBindBuffer(GL_ARRAY_BUFFER, dados_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(buffer_.size() * sizeof(float)), buffer_.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DesenhoSlices::desenhar()
{
    if (total_ <= 0 || !vao_)
        return;
    glBindVertexArray(vao_);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, total_);
    glBindVertexArray(0);
}

void montarTodas(const std::vector<FeedSlice>& slices, int canvasW, int canvasH, const std::vector<int>* permitidos,
                 float intensidade, std::vector<InstanciaSlice>& saida)
{
    saida.clear();
    if (canvasW <= 0 || canvasH <= 0)
        return;
    const float geral = std::clamp(intensidade, 0.0f, 1.0f);
    if (geral <= 0.0f)
        return;
    float indice = 0.0f;
    for (const FeedSlice& s : slices) {
        if (s.uid <= 0 || s.w <= 0.0f || s.h <= 0.0f)
            continue;
        if (permitidos && !std::binary_search(permitidos->begin(), permitidos->end(), s.uid))
            continue;
        saida.push_back(instanciaDe(s, canvasW, canvasH, geral, indice++));
    }
}

void definirVetorFloat(const Shader& shader, const char* nome, const float* valores, int quantidade)
{
    const GLint local = glGetUniformLocation(shader.id(), nome);
    if (local >= 0)
        glUniform1fv(local, quantidade, valores);
}

}
