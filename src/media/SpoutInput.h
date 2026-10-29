#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bsl {

// Receptor Spout (Spout2 SDK, SpoutGL compilado do código-fonte). Recebe imagem de qualquer programa que envie
// Spout (OpenGL, DirectX 9/11/12 via interop). Este header não inclui nada do Spout nem do GL: o Spout traz o próprio
// carregador de extensões GL, que conflita com o glad (por isso o .cpp fica isolado, como o SpoutOutput).
class SpoutInput
{
public:
    SpoutInput();
    ~SpoutInput();
    SpoutInput(const SpoutInput&) = delete;
    SpoutInput& operator=(const SpoutInput&) = delete;

    // Sender a receber; vazio = o sender ativo (o último a abrir). Trocar fecha a conexão atual.
    void setSender(const std::string& name);
    const std::string& sender() const { return sender_; }

    // Copia o quadro mais novo para a textura própria (GL_TEXTURE_2D RGBA8). Precisa do contexto GL atual.
    // A textura sai com a primeira linha da imagem em v=0 (convenção das fontes do app).
    // true = a textura tem imagem válida (conectado).
    bool receive();

    unsigned int texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool connected() const { return connected_; }
    unsigned long long frames() const { return frames_; }
    // Nome do sender ao qual está conectado (vazio se não estiver)
    const std::string& connectedName() const { return connectedName_; }

    void release();

    // Senders ativos agora (leitura da memória compartilhada do Spout; não precisa de GL)
    static std::vector<std::string> listSenders();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string sender_;
    std::string connectedName_;
    unsigned int texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool connected_ = false;
    unsigned long long frames_ = 0;
};

} // namespace bsl
