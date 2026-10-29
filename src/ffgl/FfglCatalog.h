#pragma once

// Catalogo de plugins FFGL: pastas, varredura em segundo plano (um processo por DLL), cache em disco, lista de
// bloqueados (varredura que cai ou trava; plugin que falha durante o show) e busca por typeId.
// A UI nunca espera a varredura. Seguro entre threads.

#include "ffgl/FfglInfo.h"
#include "ffgl/FfglParams.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bsl::ffgl {

struct Folder
{
    std::filesystem::path path;
    std::string label; // categoria na UI: "Resolume Extra Effects", "BSL Stage Plugins" ou o nome da pasta
};

// Um plugin conhecido (varrido com sucesso). Imutavel depois de publicado.
struct CatalogEntry
{
    PluginInfo info;
    std::string typeId;  // ffgl:<id>:<arquivo>
    std::string origin;  // rotulo da pasta de origem
    ParamMap map;        // parametros mapeados para a UI
};
using EntryPtr = std::shared_ptr<const CatalogEntry>;

struct BlockedPlugin
{
    std::string path;     // UTF-8
    std::string fileName;
    std::string origin;
    std::string reason;
    bool session = false; // true = falhou durante o uso nesta sessao (nao vai para o cache)
};

class Catalog
{
public:
    static Catalog& instance();
    ~Catalog();
    // Encerra a varredura (mata o worker em andamento) e espera a thread. Chame no fim do programa.
    void shutdown();

    // Define as pastas (varredura recursiva). Nao varre: chame rescan().
    void configure(std::vector<Folder> folders);
    // Varre em segundo plano: usa o cache para o que nao mudou (caminho + tamanho + mtime) e roda o worker no resto.
    // Se ja estiver varrendo, recomeca ao terminar a DLL atual.
    void rescan();
    // Tenta de novo uma DLL bloqueada (varredura ou sessao)
    void retry(const std::string& pathUtf8);

    bool scanning() const { return scanning_.load(); }
    std::string scanStatus() const; // "Varrendo 3 de 12…" (vazio se parado)
    // Espera a varredura acabar (testes e --demo); false se estourou o tempo
    bool waitIdle(unsigned timeoutMs);
    std::uint64_t version() const { return version_.load(); }

    std::vector<EntryPtr> entries() const;
    // Procura por typeId: id + nome do arquivo; senao so o id; senao so o nome do arquivo (plugin que mudou de pasta)
    EntryPtr find(const std::string& typeId) const;
    std::vector<BlockedPlugin> blocked() const;
    // Quantas DLLs foram ignoradas pelo filtro de PE (sem plugMain / nao x64) na ultima varredura
    int ignoredCount() const { return ignored_.load(); }
    // O WebBrowser.dll (do Resolume) apareceu numa pasta: nunca e carregado, a UI sugere a fonte Web nativa
    bool webBrowserFound() const { return webBrowser_.load(); }
    std::vector<Folder> folders() const;

    // --- Uso durante o show ---
    // O plugin falhou (excecao estruturada): fica bloqueado ate o usuario reativar
    void markSessionFault(const std::string& pathUtf8, const std::string& reason);
    bool sessionBlocked(const std::string& pathUtf8, std::string* reason = nullptr) const;
    std::uint64_t faultEpoch() const { return faultEpoch_.load(); } // muda a cada novo bloqueio de sessao

    // --- Configuracao / testes ---
    void setScanTimeoutMs(unsigned ms) { timeoutMs_ = ms; }
    void setCachePath(std::filesystem::path p);
    void setUseCache(bool on) { useCache_ = on; }
    void clearCache();

private:
    Catalog() = default;

    struct Candidate
    {
        std::filesystem::path path;
        std::string pathUtf8;
        std::string origin;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
    };
    struct CacheRecord
    {
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        bool ok = false;
        std::string error;
        PluginInfo info;
    };

    void runScan();
    void loadCache();
    void saveCache() const;
    ScanResult scanOne(const Candidate& c);
    void publish(std::vector<EntryPtr> entries, std::vector<BlockedPlugin> blocked);

    mutable std::mutex mutex_;
    std::vector<Folder> folders_;
    std::vector<EntryPtr> entries_;
    std::vector<BlockedPlugin> blocked_;                        // varredura que falhou
    std::vector<BlockedPlugin> sessionBlocked_;                 // falhou durante o uso
    std::map<std::string, CacheRecord> cache_;                  // caminho (minusculo) -> registro
    std::vector<std::string> retryPaths_;                       // DLLs a tentar de novo mesmo com cache
    std::filesystem::path cachePath_;
    std::string status_;

    std::thread thread_;
    std::atomic<bool> scanning_{false};
    std::atomic<bool> restart_{false};
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> version_{1};
    std::atomic<std::uint64_t> faultEpoch_{0};
    std::atomic<int> ignored_{0};
    std::atomic<bool> webBrowser_{false};
    std::atomic<void*> currentProc_{nullptr};                   // worker de varredura em andamento (HANDLE)
    unsigned timeoutMs_ = 10000;
    bool useCache_ = true;
    bool cacheLoaded_ = false;
};

// Plugin FFGL de um typeId "ffgl:..." para os geradores/efeitos: a mesma resolucao do Catalog::find
std::string pluginDisplayName(const PluginInfo& info); // nome, ou o nome do arquivo se vazio

} // namespace bsl::ffgl
