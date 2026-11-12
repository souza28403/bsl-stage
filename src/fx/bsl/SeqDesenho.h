#pragma once

#include "fx/bsl/SliceInstancer.h"
#include "render/Shader.h"

#include <glad/gl.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx::bsllight {

extern const char* const kVerticeSlices;

class DesenhoSlices
{
public:
    explicit DesenhoSlices(const char* fragmento, const char* vertice = kVerticeSlices);
    ~DesenhoSlices();
    DesenhoSlices(const DesenhoSlices&) = delete;
    DesenhoSlices& operator=(const DesenhoSlices&) = delete;

    bool pronto();
    const std::string& erro() const { return erro_; }
    Shader& shader() { return *shader_; }

    void carregar(const std::vector<InstanciaSlice>& instancias);
    void desenhar();
    int total() const { return total_; }

private:
    void criar();
    void liberar();

    const char* vertice_;
    const char* fragmento_;
    GLuint vao_ = 0;
    GLuint cantos_ = 0;
    GLuint dados_ = 0;
    std::vector<float> buffer_;
    std::unique_ptr<Shader> shader_;
    std::string erro_;
    bool tentou_ = false;
    int total_ = 0;
};

struct ChaveInstancias
{
    std::uint64_t slices = 0;
    std::uint64_t conjunto = 0;
    int visao = -1;
    float intensidade = -1.0f;
    bool valida = false;

    bool igual(const ChaveInstancias& o) const
    {
        return valida && o.valida && slices == o.slices && conjunto == o.conjunto && visao == o.visao &&
               intensidade == o.intensidade;
    }
};

void montarTodas(const std::vector<FeedSlice>& slices, int canvasW, int canvasH, const std::vector<int>* permitidos,
                 float intensidade, std::vector<InstanciaSlice>& saida);

void definirVetorFloat(const Shader& shader, const char* nome, const float* valores, int quantidade);

}
