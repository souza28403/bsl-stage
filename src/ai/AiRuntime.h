#pragma once

// Motor de inferencia local (ONNX Runtime + DirectML, com fallback de CPU). Sem GL: usado pelo AiEngine (thread
// propria) e pelo bslstage_aitest. O onnxruntime.dll e o DirectML.dll sao carregados em tempo de execucao, pelo caminho
// completo, da pasta do exe: se faltarem (ou o PC nao tiver DX12), a IA fica "indisponivel" e o resto do app funciona.
// Os cabecalhos do ONNX Runtime ficam so em AiRuntime.cpp.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace bsl::ai {

enum class ModelKind { Depth = 0, Person = 1 };
inline constexpr int kModelKindCount = 2;

// Onde rodar: Auto = GPU (DirectML) e, se falhar, CPU; Gpu = so GPU; Cpu = so CPU
enum class DevicePref { Auto, Gpu, Cpu };

struct ModelSpec
{
    ModelKind kind;
    const char* key;      // "depth", "person"
    const char* label;    // PT, para a UI
    const char* fileName; // em assets/models
    const char* license;
    int inputW;           // tamanho fixo de entrada (multiplo de 14 na profundidade e de 32 na pessoa)
    int inputH;
};
const ModelSpec& modelSpec(ModelKind kind);

// Pasta assets/models: ao lado do exe e, no desenvolvimento, subindo ate a raiz do projeto. Vazio se nao achar.
std::filesystem::path modelsDir();
// Testes: usa 'dir' no lugar de assets/models (enable = false volta ao normal)
void setModelsDirOverride(const std::filesystem::path& dir, bool enable = true);
std::filesystem::path modelPath(ModelKind kind); // pode nao existir
bool modelPresent(ModelKind kind);

struct AdapterInfo
{
    std::string name;
    std::uint64_t vramBytes = 0;
    bool software = false;
    bool usable = false; // aceita D3D12 (feature level 11_0)
};
// Adaptadores DXGI na ordem de desempenho (a GPU dedicada vem antes da integrada)
std::vector<AdapterInfo> listAdapters();

struct RuntimeInfo
{
    bool dllFound = false;
    bool loaded = false;
    std::string version;    // "1.24.4"
    std::string error;      // por que nao carregou
    bool gpuAvailable = false;
    std::string gpuName;    // adaptador escolhido para o DirectML
    std::string gpuError;
};
// Carrega o runtime na primeira chamada (pode demorar centenas de ms: nao chamar na thread de render)
RuntimeInfo runtimeInfo();

// O que ja se sabe do runtime, sem carregar nada e sem esperar (a thread da UI usa): tried = a carga ja comecou
struct RuntimeSnapshot
{
    bool tried = false;
    RuntimeInfo info;
};
RuntimeSnapshot peekRuntime();

// Uma sessao de um modelo (uma por modelo, usada por uma thread so)
class Session
{
public:
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // nullptr + 'error' preenchido se nao deu (modelo ausente, runtime indisponivel, DirectML falhou com Gpu...)
    static std::unique_ptr<Session> create(ModelKind kind, DevicePref pref, std::string& error);

    ModelKind kind() const { return kind_; }
    bool onGpu() const { return onGpu_; }
    const std::string& deviceName() const { return deviceName_; } // "NVIDIA GeForce RTX 4050 (DirectML)" ou "CPU"
    int inputW() const { return w_; }
    int inputH() const { return h_; }
    double lastMs() const { return lastMs_; }

    // 'rgb' = inputW x inputH x 3 (8 bits, linha 0 = topo). 'out' recebe inputW x inputH floats: profundidade relativa
    // bruta (maior = mais perto) ou alfa 0..1 da pessoa. Nao lanca excecao.
    bool run(const std::uint8_t* rgb, std::vector<float>& out, std::string& error);

private:
    Session() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ModelKind kind_ = ModelKind::Depth;
    bool onGpu_ = false;
    std::string deviceName_;
    int w_ = 0, h_ = 0;
    double lastMs_ = 0.0;
};

} // namespace bsl::ai
