#pragma once

#include <memory>
#include <string>

namespace bsl {

// Sender Spout (Spout2 SDK, SpoutGL compilado do código-fonte). Este header não inclui nada do
// Spout nem do GL: o Spout traz o próprio carregador de extensões GL, que conflita com o glad.
class SpoutOutput
{
public:
    SpoutOutput();
    ~SpoutOutput();
    SpoutOutput(const SpoutOutput&) = delete;
    SpoutOutput& operator=(const SpoutOutput&) = delete;

    // Nome do sender; trocar o nome fecha o sender atual (o novo abre no próximo send)
    void setName(const std::string& name);
    const std::string& name() const { return name_; }

    // Envia uma textura GL_TEXTURE_2D RGBA (origem embaixo, como os FBOs do projeto).
    // Precisa do contexto GL atual. false se o sender não conseguiu inicializar.
    bool send(unsigned int texture, int width, int height);

    void release();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string name_;
};

} // namespace bsl
