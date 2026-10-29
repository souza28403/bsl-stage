#pragma once

#include "fx/IEffect.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace bsl::fx {

// Um efeito numa cadeia: so os dados (serializaveis) + o IEffect criado sob demanda pelo Compositor.
struct EffectInstance
{
    std::string typeId;           // "native:blur", "isf:<caminho relativo a pasta ISF>", "ffgl:<...>"
    bool bypass = false;
    media::ParamValues params;    // so o que o usuario mudou; o resto vem dos padroes dos descritores
    bool uiOpen = true;           // UI: cabecalho expandido (nao e salvo)
    int uid = 0;                  // UI: identificador estavel durante a sessao (nao e salvo)
    bool faulted = false;         // Compositor: o efeito lancou excecao e ficou desligado (nao e salvo)

    EffectInstance();
    explicit EffectInstance(std::string type);
    // Copiar copia so os dados: o IEffect da copia nasce de novo (estado interno, como feedback, nao e duplicado)
    EffectInstance(const EffectInstance& o);
    EffectInstance& operator=(const EffectInstance& o);
    EffectInstance(EffectInstance&&) noexcept = default;
    EffectInstance& operator=(EffectInstance&&) noexcept = default;
    ~EffectInstance() = default;

    // Grava o valor e marca os parametros como alterados (o efeito os recebe no proximo quadro)
    void setParam(const std::string& id, media::Json value);
    void resetParams(); // Restaurar padroes

    // Descritores do tipo (vem do registro, sem GL; vazio se o tipo nao existe)
    std::vector<media::ParamDesc> paramDescs() const;
    std::string displayName() const;
    bool known() const;
    // Texto de estado para a UI (tipo ausente, erro de compilacao...). Vazio = tudo certo.
    media::SourceStatus status() const;

    // --- Tempo de execucao (Compositor) ---
    IEffect* ensureEffect(); // cria na primeira vez; nullptr se o tipo e desconhecido
    IEffect* effect() const { return effect_.get(); }
    void dropEffect() { effect_.reset(); } // libera os recursos GL (contexto vivo)
    void syncParams();                     // entrega os parametros ao efeito se mudaram
    // Traz para 'params' o que o proprio efeito mudou (plugin FFGL com interface propria). Nao mexe enquanto o usuario
    // acabou de editar (nao briga com o arrastar de um slider).
    void pollEffect();

    // Copia os dados de 'o' (mesmo tipo) sem recriar o efeito de execucao; false se o tipo e outro
    bool updateFrom(const EffectInstance& o);

    media::Json toJson() const;
    static EffectInstance fromJson(const media::Json& j);

private:
    std::unique_ptr<IEffect> effect_;
    unsigned revision_ = 1;
    unsigned applied_ = 0;
    bool createFailed_ = false;
    std::chrono::steady_clock::time_point lastEdit_{}; // ultima edicao do usuario (setParam/resetParams)
    unsigned registryVersion_ = 0; // versao do catalogo quando o tipo nao foi achado (tenta de novo se mudar)
};

// Sequencia de efeitos de um clip, camada ou master. A ordem importa: o primeiro e aplicado primeiro.
class EffectChain
{
public:
    static constexpr size_t kMaxEffects = 32;

    std::vector<EffectInstance> items;

    bool empty() const { return items.empty(); }
    int size() const { return static_cast<int>(items.size()); }
    bool valid(int index) const { return index >= 0 && index < size(); }
    // Algum efeito que nao esteja em bypass? (cadeia vazia ou toda em bypass = custo zero)
    bool hasActive() const;

    EffectInstance& add(const std::string& typeId);                  // no fim
    EffectInstance& insert(int index, const std::string& typeId);    // antes de 'index' (fora da faixa = no fim)
    bool remove(int index);
    bool move(int from, int to);  // 'to' = posicao final do efeito movido
    bool duplicate(int index);    // a copia fica logo depois do original
    bool setBypass(int index, bool bypass);

    // Espelha 'src' (a Previa roda uma copia da cadeia do clip): se so mudaram parametros ou bypass, os efeitos de
    // execucao sao mantidos; se a sequencia de tipos mudou, copia tudo de novo.
    void syncFrom(const EffectChain& src);

    media::Json toJson() const; // array; vazio se nao ha efeitos
    // Tolerante: ignora entradas invalidas (com aviso); tipos desconhecidos sao mantidos
    static EffectChain fromJson(const media::Json& j, std::vector<std::string>* warnings = nullptr);
};

} // namespace bsl::fx
