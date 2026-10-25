#pragma once

// Protocolo entre o app e o processo auxiliar bslstage_worker.exe (PDF, Office e Web).
// Compartilhado pelos dois lados.
//
//  * Controle: named pipe (\\.\pipe\bslstage-...) com mensagens JSON, uma por linha (UTF-8, terminada em '\n').
//    O app cria o pipe e o worker conecta. Toda mensagem tem o campo "t" (tipo).
//
//    app -> worker
//      open       {kind:"pdf"|"web", path, url, w, h, luid:[hi,lo], transport:"auto"|"texture"|"shm", params:{...}}
//      params     {params:{...}}            parametros novos (o worker le so o que conhece)
//      page       {n}                       ir para a pagina n (1..N)
//      resize     {w, h}
//      url        {url}   reload {}
//      mouse      {type:"move"|"down"|"up"|"leave", x, y, button:0|1|2, clicks, mods}
//      wheel      {x, y, dx, dy}
//      key        {type:"down"|"up"|"char", code, ch, mods}
//      transport  {use:"shm"}               o app nao conseguiu abrir a textura compartilhada
//      ping       {n}                       heartbeat (o worker responde pong)
//      quit       {}
//
//    worker -> app
//      hello      {pid}                     primeira mensagem depois de conectar
//      transport  {kind:"texture"|"shm", why}
//      tex        {w, h, handles:[a, b]}    texturas D3D11 compartilhadas (NT handles ja duplicados para o app)
//      shm        {w, h, handle}            memoria compartilhada (ShmHeader + 2 quadros RGBA)
//      frame      {i, seq, page}            quadro novo no buffer i
//      doc        {pages, title}            documento carregado (PDF) / titulo da pagina (Web)
//      status     {text, level:"info"|"warn"}   texto para a celula ("Convertendo…")
//      error      {msg, fatal}              fatal = nao adianta relancar
//      log        {level, msg}
//      pong       {n}
//
//  * Quadros:
//      - texture: duas texturas BGRA8 D3D11 (NT handle, keyed mutex). Escritor: AcquireSync(0) -> escreve ->
//        ReleaseSync(1). Leitor: AcquireSync(1) -> copia -> ReleaseSync(0).
//      - shm: mapeamento com ShmHeader seguido de dois buffers RGBA8 (alpha reta) w*h*4. Escritor: escolhe o
//        buffer que nao e o 'latest', marca seq impar, escreve, marca seq par e publica 'latest' (seqlock).

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace bsl::wp {

inline constexpr int kProtocolVersion = 1;
inline constexpr std::uint32_t kShmMagic = 0x57534C42; // "BLSW"

struct ShmHeader
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    volatile LONG latest;     // 0 ou 1 (-1 = nenhum quadro ainda)
    std::uint32_t reserved0;
    volatile LONG64 seq[2];   // impar = escrevendo
    std::uint8_t pad[24];
};
static_assert(sizeof(ShmHeader) == 64, "ShmHeader deve ocupar 64 bytes");

inline std::size_t shmFrameBytes(int w, int h)
{
    return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
}
inline std::size_t shmTotalBytes(int w, int h)
{
    return sizeof(ShmHeader) + 2u * shmFrameBytes(w, h);
}
inline std::uint8_t* shmBuffer(void* base, int w, int h, int index)
{
    return static_cast<std::uint8_t*>(base) + sizeof(ShmHeader) + static_cast<std::size_t>(index) * shmFrameBytes(w, h);
}

// Extensoes que o worker converte para PDF antes de abrir (minusculas, sem ponto)
inline constexpr const char* kOfficeExtensions[] = {"pptx", "ppt", "docx", "doc", "xlsx", "xls", "odp", "odt", "ods"};

// Nome do executavel do worker (fica ao lado do exe do app)
inline constexpr const wchar_t* kWorkerExeName = L"bslstage_worker.exe";

} // namespace bsl::wp
