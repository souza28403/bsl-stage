#pragma once

#include <memory>
#include <string>

namespace bsl::ndi {
class Runtime;
}

namespace bsl {

// Sender NDI® de uma Screen. Este header não inclui GL nem o NDI.
//
// Caminho do quadro: textura da Screen (GL, origem embaixo) -> passada de conversão na GPU (inverte as linhas e,
// no modo UYVY, empacota 4:2:2 em metade da largura) -> glReadPixels para um PBO (assíncrono, o render não espera)
// -> no quadro seguinte o PBO é mapeado e copiado para um buffer da CPU -> NDIlib_send_send_video_async_v2.
// Latência de um quadro. Sem receptores conectados nada disso roda. O ritmo segue o frame rate informado
// (quadros que chegam antes da hora são descartados).
class NdiOutput
{
public:
    explicit NdiOutput(ndi::Runtime* runtime = nullptr); // nulo = Runtime::instance()
    ~NdiOutput();
    NdiOutput(const NdiOutput&) = delete;
    NdiOutput& operator=(const NdiOutput&) = delete;

    // Nome do sender; trocar o nome fecha o sender atual (o novo abre no próximo send)
    void setName(const std::string& name);
    const std::string& name() const { return name_; }
    // UYVY (padrão, metade da banda) ou BGRA (cheio, com alpha)
    void setBgra(bool bgra);
    bool bgra() const { return bgra_; }
    void setFrameRate(float fps);

    // Envia uma textura GL_TEXTURE_2D RGBA8 (origem embaixo, como os FBOs do projeto). Precisa do contexto GL atual.
    // true = tudo certo (inclui "quadro descartado pelo ritmo" e "sem receptores"); false = runtime ausente ou
    // sender não criado.
    bool send(unsigned int texture, int width, int height);

    // Fecha o sender e libera os recursos GL (contexto GL atual)
    void release();

    bool runtimeAvailable() const;
    // Receptores conectados (atualizado a cada send)
    int connections() const { return connections_; }
    // Quadros entregues ao NDI desde que o sender abriu (diagnóstico/testes)
    unsigned long long framesSent() const { return framesSent_; }
    // Nome real do sender (o NDI acrescenta " (2)" se já existir um igual na máquina); vazio se fechado
    std::string actualName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ndi::Runtime* runtime_ = nullptr;
    std::string name_ = "BSL Stage";
    bool bgra_ = false;
    float fps_ = 30.0f;
    int connections_ = 0;
    unsigned long long framesSent_ = 0;
};

} // namespace bsl
