#pragma once

// Presets de layout da interface (Ver > Layout): dados, JSON (.bsllayout / layouts/*.json), troca de trechos do
// imgui.ini e pasta de presets. Nada aqui usa ImGui nem janela: o teste bslstage_uitest cobre tudo em console.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace bsl::ui::layout {

// IDs dos dockspaces (os mesmos de Ui.cpp)
inline constexpr unsigned kCompDockRoot = 0x42534C43; // "BSLC": modo Composicao
inline constexpr unsigned kMapDockRoot = 0x42534C4D;  // "BSLM": modo Mapeamento

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kFileExtension = ".bsllayout";

// Opcoes da Previa que o preset guarda (chaves em texto: o formato nao depende dos enums do app)
struct PreviewPrefs
{
    std::string source = "clip"; // clip | layer
    bool checker = true;         // quadriculado do alfa
    std::string fit = "fit";     // fit | fill
    bool operator==(const PreviewPrefs&) const = default;
};

struct LayoutPreset
{
    std::string name;
    std::int64_t created = 0;   // segundos desde 1970: define a ordem da lista (Alt+1..9)
    int refW = 0, refH = 0;     // area dos paineis (viewport menos as barras) quando foi salvo
    std::string dock;           // trechos do imgui.ini: janelas dos paineis + dockspaces da Composicao e do Mapeamento
    std::string builtin;        // layout pronto ativo na hora de salvar (default | bigGrid | bigOutput)
    PreviewPrefs preview;
    bool hasUiScale = false;    // "incluir o tamanho da interface" ao salvar
    std::string uiMode = "window"; // window | system | fixed
    float uiScale = 1.0f;
};

// --- JSON ---
std::string toJson(const LayoutPreset& preset);
// false (com a causa em *error) se o texto nao for um preset valido. Nunca lanca excecao.
bool fromJson(const std::string& text, LayoutPreset& out, std::string* error = nullptr);
bool readFile(const std::filesystem::path& file, LayoutPreset& out, std::string* error = nullptr);
bool writeFile(const std::filesystem::path& file, const LayoutPreset& preset, std::string* error = nullptr);

// Nome valido: sem espacos nas pontas nem caracteres de controle, no maximo 64 caracteres (em UTF-8 sem cortar no meio).
std::string sanitizeName(const std::string& name);

// --- imgui.ini ---
// Do ini completo (SaveIniSettingsToMemory): so as janelas dos paineis (Composicao e Mapeamento) e os blocos dos dois
// dockspaces. Sem o que e de outras coisas (janelas Audio/Plugins, tabelas, popups).
std::string extractDock(const std::string& fullIni);
// O dockspace da Composicao (ou do Mapeamento) esta no texto?
bool hasDockRoot(const std::string& dockIni, unsigned rootId);
// Ini completo novo: 'currentIni' com as janelas dos paineis e os dockspaces presentes em 'presetDock' trocados pelos do
// preset. Os IDs dos nos do preset sao renumerados (nao colidem com os que ficam) e os tamanhos multiplicados por
// sx/sy (janela de outro tamanho: as proporcoes se mantem). O que nao e do preset (o outro modo, outras janelas)
// continua igual.
std::string applyDock(const std::string& currentIni, const std::string& presetDock, double sx, double sy);

// --- Pasta de presets (um .json por preset) ---
class PresetStore
{
public:
    struct Entry
    {
        LayoutPreset preset;
        std::filesystem::path file;
    };

    explicit PresetStore(std::filesystem::path dir);

    const std::filesystem::path& dir() const { return dir_; }
    // Le a pasta de novo. Arquivos invalidos sao ignorados (e contados em skipped()).
    void reload();
    int skipped() const { return skipped_; }
    const std::vector<Entry>& entries() const { return entries_; }
    int count() const { return static_cast<int>(entries_.size()); }
    int find(const std::string& name) const; // sem diferenciar maiusculas; -1 = nao existe

    // Nome igual (sem diferenciar maiusculas) substitui o preset, mantendo a posicao na lista.
    bool save(LayoutPreset preset, std::string* error = nullptr);
    bool rename(int index, const std::string& newName, std::string* error = nullptr);
    bool remove(int index);
    bool exportTo(int index, const std::filesystem::path& file, std::string* error = nullptr) const;
    // Copia o arquivo para a pasta (vira o ultimo da lista; nome repetido ganha " (2)"). false se invalido.
    bool importFrom(const std::filesystem::path& file, std::string* error = nullptr, std::string* importedName = nullptr);

private:
    std::filesystem::path uniqueFile(const std::string& name) const;
    std::int64_t nextCreated() const;
    void sortEntries();

    std::filesystem::path dir_;
    std::vector<Entry> entries_;
    int skipped_ = 0;
};

} // namespace bsl::ui::layout
