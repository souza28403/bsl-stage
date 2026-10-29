#pragma once

// "Importar plugins...": copia uma DLL (ou uma pasta de plugins) para Documents\BSL Stage\Plugins, junto com as subpastas
// de dependencias que estiverem ao lado (interfaces web, Chromium...). A origem so e lida, nunca alterada.

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace bsl::ffgl {

struct ImportResult
{
    int dlls = 0;            // DLLs de plugin copiadas
    int folders = 0;         // pastas de dependencia copiadas
    int failures = 0;        // arquivos que nao puderam ser copiados (em uso, sem permissao)
    std::string message;     // resumo em portugues
    bool ok() const { return failures == 0 && dlls > 0; }
};

// Sincrono. 'source' = uma DLL ou uma pasta; 'dest' = a pasta de plugins do usuario. Nunca lanca.
ImportResult importPlugins(const std::filesystem::path& source, const std::filesystem::path& dest);

// A mesma coisa em segundo plano (a copia de um Chromium leva segundos). A UI consulta running()/result().
class ImportJob
{
public:
    ~ImportJob();
    void start(const std::filesystem::path& source, const std::filesystem::path& dest);
    bool running() const { return running_.load(); }
    // true uma unica vez quando um trabalho termina; 'out' recebe o resultado
    bool takeResult(ImportResult& out);

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    bool have_ = false;
    ImportResult result_;
};

} // namespace bsl::ffgl
