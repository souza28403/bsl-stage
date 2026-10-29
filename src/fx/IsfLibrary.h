#pragma once

// Catalogo de shaders ISF: pastas (a principal em Documents\BSL Stage\ISF + extras do usuario), varredura em segundo
// plano dos .fs (+ .vs opcional) e leitura do cabecalho. Sem GL: a compilacao acontece quando o efeito/gerador e usado.

#include "fx/IsfDoc.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bsl::fx {

struct IsfEntry
{
    std::string id;        // "isf:" + caminho relativo a pasta ISF (barras "/")
    std::filesystem::path file;
    std::string name;      // nome do arquivo sem extensao
    std::string category;  // PT
    std::string description;
    bool isEffect = false; // tem inputImage; senao e gerador
    std::string error;     // JSON invalido, etc. (doc nulo)
    std::shared_ptr<const IsfDoc> doc;
    std::vector<std::string> warnings;
};

class IsfLibrary
{
public:
    static IsfLibrary& instance();
    ~IsfLibrary();

    // Define as pastas (a primeira e a principal). Se 'samplesDir' existir e a pasta principal ainda nao existir,
    // ela e criada e recebe uma copia dos shaders de exemplo (primeiro uso). Nao varre: chame rescan().
    void configure(std::vector<std::filesystem::path> dirs, const std::filesystem::path& samplesDir = {});
    std::vector<std::filesystem::path> directories() const;

    // Varre as pastas numa thread de fundo ("Recarregar"); varreduras pedidas durante uma varredura sao reunidas
    void rescan();
    void waitScan();            // bloqueia ate acabar (testes)
    bool scanning() const { return scanning_.load(); }
    std::uint64_t version() const { return version_.load(); } // muda a cada varredura concluida

    std::vector<IsfEntry> entries() const;
    // Procura por id; se ainda nao foi varrido, tenta ler o arquivo nas pastas (sincrono, so o cabecalho)
    std::optional<IsfEntry> find(const std::string& id);
    std::shared_ptr<const IsfDoc> doc(const std::string& id);

    // Le um arquivo avulso (testes e find)
    static IsfEntry loadFile(const std::filesystem::path& file, const std::string& id);

private:
    IsfLibrary() = default;
    void scanWorker(std::vector<std::filesystem::path> dirs);

    mutable std::mutex mutex_;
    std::vector<std::filesystem::path> dirs_;
    std::vector<IsfEntry> entries_;
    std::atomic<std::uint64_t> version_{1};
    std::atomic<bool> scanning_{false};
    bool rescanQueued_ = false;
    std::thread worker_;
};

} // namespace bsl::fx
