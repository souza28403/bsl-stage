#pragma once

#include "fx/IEffect.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bsl::fx {

enum class EffectSection { Native, Isf, Ffgl, Bsl }; // Bsl: plugins BSL Light nativos (prefixo "bsl:")

// Entrada do catalogo: o que a UI mostra e o que basta para criar o efeito
struct EffectTypeInfo
{
    std::string id;          // typeId gravado no projeto: "native:blur", "isf:ondas.fs", "ffgl:<...>"
    std::string name;        // PT
    std::string category;    // PT
    const char* icon = "";   // ICON_*
    EffectSection section = EffectSection::Native;
    std::string description;
    std::string error;       // preenchido = nao foi possivel carregar (ex.: JSON do ISF invalido)
    bool generator = false;  // sem entrada de imagem (ISF gerador): nao e oferecido como efeito
    bool ai = false;         // usa IA local (selo "IA" na lista): nada sai do PC
};

// Uma fonte de tipos de efeito: nativos, shaders ISF e (Fase 4B) plugins FFGL
class IEffectProvider
{
public:
    virtual ~IEffectProvider() = default;
    virtual std::string prefix() const = 0; // "native:", "isf:", "ffgl:"
    virtual std::vector<EffectTypeInfo> list() const = 0;
    // Um tipo so (o padrao varre list(); provedores grandes sobrescrevem com uma busca direta)
    virtual std::optional<EffectTypeInfo> info(const std::string& typeId) const
    {
        for (EffectTypeInfo& t : list())
            if (t.id == typeId)
                return std::move(t);
        return std::nullopt;
    }
    virtual std::unique_ptr<IEffect> create(const std::string& typeId) const = 0; // nullptr se desconhecido
    // Parametros sem precisar criar o efeito (sem GL); vazio se desconhecido
    virtual std::vector<media::ParamDesc> describe(const std::string& typeId) const = 0;
    // Muda quando list() muda (reescaneio de pastas, etc.)
    virtual std::uint64_t version() const { return 0; }
};

class EffectRegistry
{
public:
    // Ja vem com os nativos e o ISF registrados
    static EffectRegistry& instance();

    void registerProvider(std::shared_ptr<IEffectProvider> provider);

    std::vector<EffectTypeInfo> types() const; // todos os provedores, em ordem de registro
    std::optional<EffectTypeInfo> find(const std::string& typeId) const;
    std::unique_ptr<IEffect> create(const std::string& typeId) const;
    std::vector<media::ParamDesc> describe(const std::string& typeId) const;
    std::uint64_t version() const; // muda quando qualquer provedor muda o catalogo

private:
    EffectRegistry() = default;
    const IEffectProvider* providerFor(const std::string& typeId) const;
    std::vector<std::shared_ptr<IEffectProvider>> providers_;
};

// Provedores embutidos (EffectRegistry::instance() os registra)
std::shared_ptr<IEffectProvider> makeNativeProvider();
std::shared_ptr<IEffectProvider> makeIsfProvider();
std::shared_ptr<IEffectProvider> makeFfglProvider(); // ffgl/FfglProvider.cpp
std::shared_ptr<IEffectProvider> makeBslProvider();  // fx/bsl/BslProvider.cpp

} // namespace bsl::fx
