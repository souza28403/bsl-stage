// ndi_min.h - cabeçalho MÍNIMO e de autoria própria para falar com o runtime do NDI®.
//
// Não faz parte do NDI SDK e não contém código nem texto dele. Declara só o subconjunto da API C que o BSL Stage
// usa (descoberta de fontes, receptor e emissor de vídeo), escrito a partir da documentação pública do NDI 5/6
// (https://docs.ndi.video) e conferido com o runtime oficial nesta máquina:
//   * layout dos structs: regras normais de alinhamento do C (x64), checadas pelos static_assert abaixo e pelo
//     bslstage_ndittest (um quadro conhecido enviado e recebido de volta);
//   * ordem da tabela de funções: a tabela devolvida por NDIlib_v5_load() foi comparada, posição por posição, com os
//     símbolos exportados pela DLL oficial (NDIlib_initialize, NDIlib_find_create_v2, ...). Os nomes dos campos
//     abaixo seguem essa ordem. Os campos que não usamos ficam como void* só para segurar o lugar.
//
// O runtime é carregado em tempo de execução (LoadLibrary): nada do NDI é linkado nem distribuído por este projeto.
// A tabela v6 (NDIlib_v6_load) é uma extensão da v5 que preserva o prefixo; usamos só o prefixo, então o mesmo
// struct vale para as duas.
//
// NDI® é marca registrada da Vizrt NDI AB.
#pragma once

#include <cstddef>
#include <cstdint>

static_assert(sizeof(void*) == 8, "ndi_min.h cobre só x64 (Processing.NDI.Lib.x64.dll)");

// --------------------------------------------------------------------------------------------------------------
// Tipos opacos
struct NDIlib_find_instance_opaque;
struct NDIlib_recv_instance_opaque;
struct NDIlib_send_instance_opaque;
using NDIlib_find_instance_t = NDIlib_find_instance_opaque*;
using NDIlib_recv_instance_t = NDIlib_recv_instance_opaque*;
using NDIlib_send_instance_t = NDIlib_send_instance_opaque*;

// --------------------------------------------------------------------------------------------------------------
// Enumerações (valores da documentação pública)
enum NDIlib_frame_type_e : int32_t
{
    NDIlib_frame_type_none = 0,
    NDIlib_frame_type_video = 1,
    NDIlib_frame_type_audio = 2,
    NDIlib_frame_type_metadata = 3,
    NDIlib_frame_type_error = 4,
    NDIlib_frame_type_status_change = 100, // a fonte mudou de estado (conexão, tally...)
    NDIlib_frame_type_source_change = 101
};

enum NDIlib_recv_bandwidth_e : int32_t
{
    NDIlib_recv_bandwidth_metadata_only = -10,
    NDIlib_recv_bandwidth_audio_only = 10,
    NDIlib_recv_bandwidth_lowest = 0, // fluxo de baixa qualidade/proxy
    NDIlib_recv_bandwidth_highest = 100
};

enum NDIlib_recv_color_format_e : int32_t
{
    NDIlib_recv_color_format_BGRX_BGRA = 0,
    NDIlib_recv_color_format_UYVY_BGRA = 1, // UYVY sem alpha, BGRA com alpha
    NDIlib_recv_color_format_RGBX_RGBA = 2,
    NDIlib_recv_color_format_UYVY_RGBA = 3,
    NDIlib_recv_color_format_fastest = 100,
    NDIlib_recv_color_format_best = 101
};

// FourCC = os 4 caracteres em little-endian
constexpr uint32_t NDIlib_make_fourcc(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
           | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

enum NDIlib_FourCC_video_type_e : uint32_t
{
    NDIlib_FourCC_video_type_UYVY = NDIlib_make_fourcc('U', 'Y', 'V', 'Y'), // 4:2:2 empacotado, bytes U Y0 V Y1
    NDIlib_FourCC_video_type_UYVA = NDIlib_make_fourcc('U', 'Y', 'V', 'A'),
    NDIlib_FourCC_video_type_BGRA = NDIlib_make_fourcc('B', 'G', 'R', 'A'),
    NDIlib_FourCC_video_type_BGRX = NDIlib_make_fourcc('B', 'G', 'R', 'X'),
    NDIlib_FourCC_video_type_RGBA = NDIlib_make_fourcc('R', 'G', 'B', 'A'),
    NDIlib_FourCC_video_type_RGBX = NDIlib_make_fourcc('R', 'G', 'B', 'X')
};

enum NDIlib_frame_format_type_e : int32_t
{
    NDIlib_frame_format_type_progressive = 1,
    NDIlib_frame_format_type_interleaved = 0,
    NDIlib_frame_format_type_field_0 = 2,
    NDIlib_frame_format_type_field_1 = 3
};

// Carimbo de tempo "sintetize para mim"
constexpr int64_t NDIlib_send_timecode_synthesize = INT64_MAX;

// --------------------------------------------------------------------------------------------------------------
// Structs
struct NDIlib_source_t
{
    const char* p_ndi_name;    // UTF-8, "MAQUINA (Nome da fonte)"
    const char* p_url_address; // UTF-8, pode ser nulo (a união p_ip_address da documentação ocupa o mesmo lugar)
};
static_assert(sizeof(NDIlib_source_t) == 16);

struct NDIlib_find_create_t
{
    bool show_local_sources;   // inclui as fontes desta máquina
    const char* p_groups;      // nulo = grupo padrão
    const char* p_extra_ips;   // nulo = só descoberta automática
};
static_assert(sizeof(NDIlib_find_create_t) == 24);
static_assert(offsetof(NDIlib_find_create_t, p_groups) == 8);

struct NDIlib_recv_create_v3_t
{
    NDIlib_source_t source_to_connect_to;
    NDIlib_recv_color_format_e color_format;
    NDIlib_recv_bandwidth_e bandwidth;
    bool allow_video_fields;
    const char* p_ndi_recv_name; // nome do receptor (aparece para o emissor)
};
static_assert(offsetof(NDIlib_recv_create_v3_t, color_format) == 16);
static_assert(offsetof(NDIlib_recv_create_v3_t, allow_video_fields) == 24);
static_assert(offsetof(NDIlib_recv_create_v3_t, p_ndi_recv_name) == 32);
static_assert(sizeof(NDIlib_recv_create_v3_t) == 40);

struct NDIlib_send_create_t
{
    const char* p_ndi_name;
    const char* p_groups;
    bool clock_video; // true = send_video espera o ritmo do frame rate
    bool clock_audio;
};
static_assert(sizeof(NDIlib_send_create_t) == 24);

struct NDIlib_video_frame_v2_t
{
    int xres, yres;
    NDIlib_FourCC_video_type_e FourCC;
    int frame_rate_N, frame_rate_D;
    float picture_aspect_ratio; // largura/altura; 0 = xres/yres
    NDIlib_frame_format_type_e frame_format_type;
    int64_t timecode;
    uint8_t* p_data;
    int line_stride_in_bytes; // (a união data_size_in_bytes da documentação ocupa o mesmo lugar)
    const char* p_metadata;   // XML, pode ser nulo
    int64_t timestamp;        // preenchido pelo receptor
};
static_assert(offsetof(NDIlib_video_frame_v2_t, FourCC) == 8);
static_assert(offsetof(NDIlib_video_frame_v2_t, picture_aspect_ratio) == 20);
static_assert(offsetof(NDIlib_video_frame_v2_t, timecode) == 32);
static_assert(offsetof(NDIlib_video_frame_v2_t, p_data) == 40);
static_assert(offsetof(NDIlib_video_frame_v2_t, line_stride_in_bytes) == 48);
static_assert(offsetof(NDIlib_video_frame_v2_t, p_metadata) == 56);
static_assert(offsetof(NDIlib_video_frame_v2_t, timestamp) == 64);
static_assert(sizeof(NDIlib_video_frame_v2_t) == 72);

struct NDIlib_recv_queue_t
{
    int video_frames;
    int audio_frames;
    int metadata_frames;
};

// --------------------------------------------------------------------------------------------------------------
// Tabela de funções devolvida por NDIlib_v5_load() / NDIlib_v6_load() (ver nota no topo sobre a ordem).
// Convenção de chamada: a padrão do x64.
struct NDIlib_v5_t
{
    bool (*initialize)(void);                                                             // 0
    void (*destroy)(void);                                                                // 1
    const char* (*version)(void);                                                         // 2
    bool (*is_supported_CPU)(void);                                                       // 3
    void* find_create;                                                                    // 4  (obsoleto)
    NDIlib_find_instance_t (*find_create_v2)(const NDIlib_find_create_t* p_settings);     // 5
    void (*find_destroy)(NDIlib_find_instance_t inst);                                    // 6
    void* find_get_sources;                                                               // 7  (obsoleto)
    NDIlib_send_instance_t (*send_create)(const NDIlib_send_create_t* p_settings);        // 8
    void (*send_destroy)(NDIlib_send_instance_t inst);                                    // 9
    void* send_send_video;                                                                // 10 (obsoleto)
    void* send_send_video_async;                                                          // 11 (obsoleto)
    void* send_send_audio;                                                                // 12
    void* send_send_metadata;                                                             // 13
    void* send_capture;                                                                   // 14
    void* send_free_metadata;                                                             // 15
    void* send_get_tally;                                                                 // 16
    int (*send_get_no_connections)(NDIlib_send_instance_t inst, uint32_t timeout_ms);     // 17
    void* send_clear_connection_metadata;                                                 // 18
    void* send_add_connection_metadata;                                                   // 19
    void* send_set_failover;                                                              // 20
    void* recv_create_v2;                                                                 // 21 (obsoleto)
    void* recv_create;                                                                    // 22 (obsoleto)
    void (*recv_destroy)(NDIlib_recv_instance_t inst);                                    // 23
    void* recv_capture;                                                                   // 24 (obsoleto)
    void* recv_free_video;                                                                // 25 (obsoleto)
    void* recv_free_audio;                                                                // 26
    void* recv_free_metadata;                                                             // 27
    void* recv_send_metadata;                                                             // 28
    void* recv_set_tally;                                                                 // 29
    void* recv_get_performance;                                                           // 30
    void (*recv_get_queue)(NDIlib_recv_instance_t inst, NDIlib_recv_queue_t* p_total);    // 31
    void* recv_clear_connection_metadata;                                                 // 32
    void* recv_add_connection_metadata;                                                   // 33
    int (*recv_get_no_connections)(NDIlib_recv_instance_t inst);                          // 34
    void* routing_create;                                                                 // 35
    void* routing_destroy;                                                                // 36
    void* routing_change;                                                                 // 37
    void* routing_clear;                                                                  // 38
    void* util_send_send_audio_interleaved_16s;                                           // 39
    void* util_audio_to_interleaved_16s;                                                  // 40
    void* util_audio_from_interleaved_16s;                                                // 41
    bool (*find_wait_for_sources)(NDIlib_find_instance_t inst, uint32_t timeout_ms);      // 42
    const NDIlib_source_t* (*find_get_current_sources)(NDIlib_find_instance_t inst, uint32_t* p_no_sources); // 43
    void* util_audio_to_interleaved_32f;                                                  // 44
    void* util_audio_from_interleaved_32f;                                                // 45
    void* util_send_send_audio_interleaved_32f;                                           // 46
    void (*recv_free_video_v2)(NDIlib_recv_instance_t inst, const NDIlib_video_frame_v2_t* p_frame); // 47
    void* recv_free_audio_v2;                                                             // 48
    void* recv_capture_v2;                                                                // 49
    void (*send_send_video_v2)(NDIlib_send_instance_t inst, const NDIlib_video_frame_v2_t* p_frame);       // 50
    void (*send_send_video_async_v2)(NDIlib_send_instance_t inst, const NDIlib_video_frame_v2_t* p_frame); // 51
    void* send_send_audio_v2;                                                             // 52
    void* util_audio_to_interleaved_16s_v2;                                               // 53
    void* util_audio_from_interleaved_16s_v2;                                             // 54
    void* util_audio_to_interleaved_32f_v2;                                               // 55
    void* util_audio_from_interleaved_32f_v2;                                             // 56
    void* recv_free_string;                                                               // 57
    void* recv_ptz_is_supported;                                                          // 58
    void* recv_recording_is_supported;                                                    // 59
    void* recv_get_web_control;                                                           // 60
    void* recv_ptz_zoom;                                                                  // 61
    void* recv_ptz_zoom_speed;                                                            // 62
    void* recv_ptz_pan_tilt;                                                              // 63
    void* recv_ptz_pan_tilt_speed;                                                        // 64
    void* recv_ptz_store_preset;                                                          // 65
    void* recv_ptz_recall_preset;                                                         // 66
    void* recv_ptz_auto_focus;                                                            // 67
    void* recv_ptz_focus;                                                                 // 68
    void* recv_ptz_focus_speed;                                                           // 69
    void* recv_ptz_white_balance_auto;                                                    // 70
    void* recv_ptz_white_balance_indoor;                                                  // 71
    void* recv_ptz_white_balance_outdoor;                                                 // 72
    void* recv_ptz_white_balance_oneshot;                                                 // 73
    void* recv_ptz_white_balance_manual;                                                  // 74
    void* recv_ptz_exposure_auto;                                                         // 75
    void* recv_ptz_exposure_manual;                                                       // 76
    void* recv_recording_start;                                                           // 77
    void* recv_recording_stop;                                                            // 78
    void* recv_recording_set_audio_level;                                                 // 79
    void* recv_recording_is_recording;                                                    // 80
    void* recv_recording_get_filename;                                                    // 81
    void* recv_recording_get_error;                                                       // 82
    void* recv_recording_get_times;                                                       // 83
    NDIlib_recv_instance_t (*recv_create_v3)(const NDIlib_recv_create_v3_t* p_settings);  // 84
    void (*recv_connect)(NDIlib_recv_instance_t inst, const NDIlib_source_t* p_src);      // 85 (nulo desconecta)
    void* framesync_create;                                                               // 86
    void* framesync_destroy;                                                              // 87
    void* framesync_capture_audio;                                                        // 88
    void* framesync_free_audio;                                                           // 89
    void* framesync_capture_video;                                                        // 90
    void* framesync_free_video;                                                           // 91
    void* util_send_send_audio_interleaved_32s;                                           // 92
    void* util_audio_to_interleaved_32s_v2;                                               // 93
    void* util_audio_from_interleaved_32s_v2;                                             // 94
    const NDIlib_source_t* (*send_get_source_name)(NDIlib_send_instance_t inst);          // 95
    void* send_send_audio_v3;                                                             // 96
    void* util_V210_to_P216;                                                              // 97
    void* util_P216_to_V210;                                                              // 98
    void* routing_get_no_connections;                                                     // 99
    void* routing_get_source_name;                                                        // 100
    // Os ponteiros de áudio e de metadados são nulos nas nossas chamadas (descartamos áudio e metadados)
    NDIlib_frame_type_e (*recv_capture_v3)(NDIlib_recv_instance_t inst, NDIlib_video_frame_v2_t* p_video,
                                           void* p_audio, void* p_metadata, uint32_t timeout_ms); // 101
    void* recv_free_audio_v3;                                                             // 102
    void* framesync_capture_audio_v2;                                                     // 103
    void* framesync_free_audio_v2;                                                        // 104
    void* framesync_audio_queue_depth;                                                    // 105
    void* recv_ptz_exposure_manual_v2;                                                    // 106
};
static_assert(offsetof(NDIlib_v5_t, find_create_v2) == 5 * 8);
static_assert(offsetof(NDIlib_v5_t, send_get_no_connections) == 17 * 8);
static_assert(offsetof(NDIlib_v5_t, find_wait_for_sources) == 42 * 8);
static_assert(offsetof(NDIlib_v5_t, send_send_video_v2) == 50 * 8);
static_assert(offsetof(NDIlib_v5_t, recv_create_v3) == 84 * 8);
static_assert(offsetof(NDIlib_v5_t, recv_capture_v3) == 101 * 8);
static_assert(sizeof(NDIlib_v5_t) == 107 * 8);

// Exportada pela DLL: devolve a tabela (memória da própria DLL; não liberar)
using NDIlib_load_fn = const NDIlib_v5_t* (*)(void);
