#pragma once

// Ponte entre spouttest.cpp (usa o SDK do Spout e o GL dele) e spouttest_source.cpp (usa o app: glad, ISource).
// Sem nenhum header de GL aqui.

#include <functional>
#include <string>
#include <vector>

namespace spouttest {

struct AppSourceResult
{
    bool registered = false;  // o gerador "Spout" existe no registro e e uma fonte ao vivo
    bool isLive = false;
    bool listed = false;      // o sender apareceu na lista de senders ativos
    bool connected = false;
    int width = 0;
    int height = 0;
    std::string status;
    std::vector<unsigned char> rgba; // textura da fonte, na ordem da memoria (linha 0 = topo da imagem)
};

// Abre a fonte Spout do app apontando para 'sender' e a roda ate receber quadros. 'sendOne' deve enviar um quadro
// do sender (e chamado em cada volta). Precisa do contexto GL atual.
bool receiveWithAppSource(const std::string& sender, const std::function<void()>& sendOne, AppSourceResult& out);

} // namespace spouttest
