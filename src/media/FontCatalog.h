#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl::media {

struct FontEntry
{
    std::string file;  // nome do arquivo (chave gravada no projeto), ex.: "Barlow-Bold.ttf"
    std::string label; // nome para exibir, ex.: "Barlow Bold"
    std::filesystem::path path;
    bool bundled = false; // vem em assets/fonts
};

// Fontes disponiveis para o gerador de Texto: assets/fonts + pasta de fontes do Windows (+ fontes do usuario).
// A varredura le so a tabela 'name' de cada arquivo, numa thread de fundo; entries() fica vazio ate terminar.
class FontCatalog
{
public:
    static FontCatalog& instance();

    // Inicia a varredura (uma vez). Pode ser chamado a cada frame.
    void ensureScanned();
    bool ready() const;
    std::vector<FontEntry> entries() const; // copia (ordenada por nome)

    // Acha o arquivo da fonte pela chave (nome do arquivo). Vazio se nao existir. Nao depende da varredura.
    std::filesystem::path resolve(const std::string& file) const;
    // Fonte padrao do texto (Barlow Bold do app, ou uma fonte do sistema)
    std::string defaultFile() const;
    // Nome amigavel de uma fonte conhecida (varredura pronta); senao o proprio nome do arquivo
    std::string labelFor(const std::string& file) const;

    ~FontCatalog();

private:
    FontCatalog();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Le "familia estilo" da tabela 'name' de um .ttf/.otf/.ttc (primeira fonte da colecao). false se invalido.
bool readFontLabel(const std::filesystem::path& file, std::string& label);

} // namespace bsl::media
