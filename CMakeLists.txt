cmake_minimum_required(VERSION 3.25)
project(BSLStage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

if(WIN32)
    set(_bsl_default ON)
else()
    set(_bsl_default OFF)
endif()
option(BSL_WITH_SPOUT "Troca de video via Spout" ${_bsl_default})
option(BSL_WITH_D3D "Interoperabilidade Direct3D" ${_bsl_default})
option(BSL_WITH_CEF "Navegador embutido (CEF)" ${_bsl_default})
if(WIN32 OR (UNIX AND NOT APPLE))
    set(_bsl_ai_default ON)
else()
    set(_bsl_ai_default OFF)
endif()
option(BSL_WITH_AI "Efeitos de IA (ONNX Runtime)" ${_bsl_ai_default})
option(BSL_WITH_SCREENCAP "Captura de tela e janela" ${_bsl_default})
option(BSL_WITH_HOTKEYS "Atalhos globais do sistema" ${_bsl_default})
option(BSL_WITH_UPDATER "Atualizador" ${_bsl_default})
option(BSL_WITH_MSI "Instalador MSI e licenca por Custom Action" ${_bsl_default})
option(BSL_WITH_FFGL "Plugins FFGL" ${_bsl_default})
if(WIN32)
    set(_bsl_ativacao_default OFF)
else()
    set(_bsl_ativacao_default ON)
endif()
option(BSL_WITH_ATIVACAO "Tela de ativacao da licenca dentro do app" ${_bsl_ativacao_default})

set(BSL_CEF_DIR "C:/Users/GABRIEL - BSL LIGHT/Desktop/ffgl-master/deps/cef" CACHE PATH
    "Distribuicao binaria do CEF para a fonte Web. Vazio ou inexistente = Web desabilitada")
set(BSL_HAVE_CEF OFF)
if(BSL_WITH_CEF AND BSL_CEF_DIR AND EXISTS "${BSL_CEF_DIR}/include/cef_version.h" AND EXISTS "${BSL_CEF_DIR}/Release/libcef.lib")
    set(BSL_HAVE_CEF ON)
endif()

set(_bsl_flags SPOUT D3D AI SCREENCAP HOTKEYS UPDATER MSI FFGL ATIVACAO)
foreach(_f IN LISTS _bsl_flags)
    if(BSL_WITH_${_f})
        add_compile_definitions(BSL_WITH_${_f}=1)
    else()
        add_compile_definitions(BSL_WITH_${_f}=0)
    endif()
endforeach()
if(BSL_HAVE_CEF)
    add_compile_definitions(BSL_WITH_CEF=1)
else()
    add_compile_definitions(BSL_WITH_CEF=0)
endif()

if(WIN32)
    set(BSL_SHELL_LIBS shell32 ole32)
else()
    set(BSL_SHELL_LIBS "")
endif()
if(BSL_WITH_SPOUT)
    set(BSL_SPOUT_LIB spoutgl)
else()
    set(BSL_SPOUT_LIB "")
endif()

set(BSLSTAGE_GITHUB_REPO "souza28403/bsl-stage" CACHE STRING "Repositorio do GitHub (dono/nome) usado nas atualizacoes e nos reportes")
configure_file(src/app/Version.h.in "${CMAKE_BINARY_DIR}/generated/app/Version.h" @ONLY)

set(BSL_ICON_RC "${CMAKE_SOURCE_DIR}/assets/icons/BSLStage.ico")
function(bsl_add_version_resource target description internal_name)
    if(NOT WIN32)
        return()
    endif()
    set(BSL_RC_DESCRIPTION "${description}")
    set(BSL_RC_INTERNAL "${internal_name}")
    set(_rc "${CMAKE_BINARY_DIR}/generated/app/${internal_name}.rc")
    configure_file("${CMAKE_SOURCE_DIR}/src/app/BSLStage.rc.in" "${_rc}" @ONLY)
    target_sources(${target} PRIVATE "${_rc}")
endfunction()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0-docking
    GIT_SHALLOW TRUE)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)

FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.zip)

FetchContent_Declare(spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG 2.007.018
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR bsl_sem_cmake_do_spout)

FetchContent_Declare(monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG 4.0.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR bsl_sem_cmake_do_monocypher)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.26
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR bsl_sem_cmake_do_miniaudio)

FetchContent_MakeAvailable(glfw imgui nlohmann_json stb monocypher)
if(NOT WIN32)
    FetchContent_MakeAvailable(miniaudio)
endif()
if(BSL_WITH_SPOUT)
    FetchContent_MakeAvailable(spout2)
endif()

if(WIN32)
    set(BSL_FFMPEG_VERSION "9.0")
    set(BSL_FFMPEG_PKG "ffmpeg-n${BSL_FFMPEG_VERSION}-latest-win64-lgpl-shared-${BSL_FFMPEG_VERSION}")
    set(BSL_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/${BSL_FFMPEG_PKG}")
    if(NOT EXISTS "${BSL_FFMPEG_ROOT}/include/libavformat/avformat.h")
        set(_zip "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/${BSL_FFMPEG_PKG}.zip")
        message(STATUS "Baixando FFmpeg ${BSL_FFMPEG_VERSION} (BtbN, LGPL shared)...")
        file(DOWNLOAD
            "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/${BSL_FFMPEG_PKG}.zip"
            "${_zip}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON)
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            file(REMOVE "${_zip}")
            message(FATAL_ERROR "Falha ao baixar o FFmpeg: ${_dl_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/ffmpeg")
        file(REMOVE "${_zip}")
    endif()

    add_library(ffmpeg_ffmpeg INTERFACE)
    add_library(ffmpeg::ffmpeg ALIAS ffmpeg_ffmpeg)
    target_include_directories(ffmpeg_ffmpeg SYSTEM INTERFACE "${BSL_FFMPEG_ROOT}/include")
    target_link_libraries(ffmpeg_ffmpeg INTERFACE
        "${BSL_FFMPEG_ROOT}/lib/avformat.lib"
        "${BSL_FFMPEG_ROOT}/lib/avcodec.lib"
        "${BSL_FFMPEG_ROOT}/lib/avutil.lib"
        "${BSL_FFMPEG_ROOT}/lib/swscale.lib"
        "${BSL_FFMPEG_ROOT}/lib/avdevice.lib")
    file(GLOB BSL_FFMPEG_DLLS
        "${BSL_FFMPEG_ROOT}/bin/avformat-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/avcodec-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/avutil-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/swscale-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/swresample-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/avdevice-*.dll"
        "${BSL_FFMPEG_ROOT}/bin/avfilter-*.dll")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BSL_FFMPEG_PKGCFG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample libavdevice)
    add_library(ffmpeg_ffmpeg INTERFACE)
    add_library(ffmpeg::ffmpeg ALIAS ffmpeg_ffmpeg)
    target_link_libraries(ffmpeg_ffmpeg INTERFACE PkgConfig::BSL_FFMPEG_PKGCFG)
    set(BSL_FFMPEG_DLLS "")
endif()

set(BSL_PDFIUM_TAG "8111")
set(BSL_PDFIUM_SHA256_LINUX "" CACHE STRING "SHA256 do pacote PDFium para Linux")
set(BSL_PDFIUM_SHA256_MAC "" CACHE STRING "SHA256 do pacote PDFium para macOS")
if(WIN32)
    set(BSL_PDFIUM_PLATFORM "win-x64")
    set(BSL_PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium/chromium-${BSL_PDFIUM_TAG}")
    set(BSL_PDFIUM_LIB "${BSL_PDFIUM_ROOT}/lib/pdfium.dll.lib")
    set(BSL_PDFIUM_RUNTIME "${BSL_PDFIUM_ROOT}/bin/pdfium.dll")
    set(BSL_PDFIUM_SHA256 "")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(BSL_PDFIUM_PLATFORM "mac-arm64")
    else()
        set(BSL_PDFIUM_PLATFORM "mac-x64")
    endif()
    set(BSL_PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium/chromium-${BSL_PDFIUM_TAG}-${BSL_PDFIUM_PLATFORM}")
    set(BSL_PDFIUM_LIB "${BSL_PDFIUM_ROOT}/lib/libpdfium.dylib")
    set(BSL_PDFIUM_RUNTIME "${BSL_PDFIUM_LIB}")
    set(BSL_PDFIUM_SHA256 "${BSL_PDFIUM_SHA256_MAC}")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(BSL_PDFIUM_PLATFORM "linux-arm64")
    else()
        set(BSL_PDFIUM_PLATFORM "linux-x64")
    endif()
    set(BSL_PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium/chromium-${BSL_PDFIUM_TAG}-${BSL_PDFIUM_PLATFORM}")
    set(BSL_PDFIUM_LIB "${BSL_PDFIUM_ROOT}/lib/libpdfium.so")
    set(BSL_PDFIUM_RUNTIME "${BSL_PDFIUM_LIB}")
    set(BSL_PDFIUM_SHA256 "${BSL_PDFIUM_SHA256_LINUX}")
endif()
if(NOT EXISTS "${BSL_PDFIUM_ROOT}/include/fpdfview.h")
    set(_tgz "${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium-${BSL_PDFIUM_PLATFORM}-${BSL_PDFIUM_TAG}.tgz")
    message(STATUS "Baixando PDFium chromium/${BSL_PDFIUM_TAG} ${BSL_PDFIUM_PLATFORM} (bblanchon/pdfium-binaries)...")
    file(MAKE_DIRECTORY "${BSL_PDFIUM_ROOT}")
    set(_hash_arg "")
    if(BSL_PDFIUM_SHA256)
        set(_hash_arg EXPECTED_HASH SHA256=${BSL_PDFIUM_SHA256})
    endif()
    file(DOWNLOAD
        "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/${BSL_PDFIUM_TAG}/pdfium-${BSL_PDFIUM_PLATFORM}.tgz"
        "${_tgz}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON ${_hash_arg})
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${_tgz}")
        message(FATAL_ERROR "Falha ao baixar o PDFium: ${_dl_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_tgz}" DESTINATION "${BSL_PDFIUM_ROOT}")
    file(REMOVE "${_tgz}")
endif()

set(BSL_ORT_VERSION "1.24.4")
set(BSL_ORT_SHA256 "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")
set(BSL_DML_VERSION "1.15.4")
set(BSL_DML_SHA256 "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9")
set(BSL_ORT_SHA256_LINUX "3a211fbea252c1e66290658f1b735b772056149f28321e71c308942cdb54b747" CACHE STRING "SHA256 do pacote ONNX Runtime para Linux")
set(BSL_ORT_SHA256_MAC "" CACHE STRING "SHA256 do pacote ONNX Runtime para macOS")
set(BSL_ORT_ROOT "")
set(BSL_DML_ROOT "")
set(BSL_ORT_INCLUDE "")
set(BSL_DML_INCLUDE "")
set(BSL_ORT_DLLS "")
function(bsl_fetch_nupkg name version sha256 destDir)
    set(_zip "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${name}-${version}.zip")
    message(STATUS "Baixando ${name} ${version} (NuGet)...")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
    file(DOWNLOAD "https://api.nuget.org/v3-flatcontainer/${name}/${version}/${name}.${version}.nupkg"
        "${_zip}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON EXPECTED_HASH SHA256=${sha256})
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${_zip}")
        message(FATAL_ERROR "Falha ao baixar ${name}: ${_dl_status}")
    endif()
    file(MAKE_DIRECTORY "${destDir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${destDir}")
    file(REMOVE "${_zip}")
endfunction()
if(BSL_WITH_AI AND WIN32)
    set(BSL_ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/ort-${BSL_ORT_VERSION}")
    set(BSL_DML_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/directml-${BSL_DML_VERSION}")
    if(NOT EXISTS "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll")
        bsl_fetch_nupkg("microsoft.ml.onnxruntime.directml" "${BSL_ORT_VERSION}" "${BSL_ORT_SHA256}" "${BSL_ORT_ROOT}")
    endif()
    if(NOT EXISTS "${BSL_DML_ROOT}/bin/x64-win/DirectML.dll")
        bsl_fetch_nupkg("microsoft.ai.directml" "${BSL_DML_VERSION}" "${BSL_DML_SHA256}" "${BSL_DML_ROOT}")
    endif()
    set(BSL_ORT_INCLUDE "${BSL_ORT_ROOT}/build/native/include")
    set(BSL_DML_INCLUDE "${BSL_DML_ROOT}/include")
    set(BSL_ORT_DLLS
        "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll"
        "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
        "${BSL_DML_ROOT}/bin/x64-win/DirectML.dll")
elseif(BSL_WITH_AI)
    if(APPLE)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64" OR CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
            set(_ort_pkg "onnxruntime-osx-arm64-${BSL_ORT_VERSION}")
        else()
            set(_ort_pkg "onnxruntime-osx-x86_64-${BSL_ORT_VERSION}")
        endif()
        set(_ort_sha "${BSL_ORT_SHA256_MAC}")
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(_ort_pkg "onnxruntime-linux-aarch64-${BSL_ORT_VERSION}")
        else()
            set(_ort_pkg "onnxruntime-linux-x64-${BSL_ORT_VERSION}")
        endif()
        set(_ort_sha "${BSL_ORT_SHA256_LINUX}")
    endif()
    set(BSL_ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${_ort_pkg}")
    if(NOT EXISTS "${BSL_ORT_ROOT}/include/onnxruntime_c_api.h")
        set(_tgz "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/${_ort_pkg}.tgz")
        message(STATUS "Baixando ${_ort_pkg} (ONNX Runtime)...")
        file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
        set(_hash_arg "")
        if(_ort_sha)
            set(_hash_arg EXPECTED_HASH SHA256=${_ort_sha})
        endif()
        file(DOWNLOAD "https://github.com/microsoft/onnxruntime/releases/download/v${BSL_ORT_VERSION}/${_ort_pkg}.tgz"
            "${_tgz}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON ${_hash_arg})
        list(GET _dl_status 0 _dl_code)
        if(NOT _dl_code EQUAL 0)
            file(REMOVE "${_tgz}")
            message(FATAL_ERROR "Falha ao baixar o ONNX Runtime: ${_dl_status}")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_tgz}" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
        file(REMOVE "${_tgz}")
    endif()
    set(BSL_ORT_INCLUDE "${BSL_ORT_ROOT}/include")
    if(APPLE)
        file(GLOB _ort_libs "${BSL_ORT_ROOT}/lib/libonnxruntime.*.dylib")
        set(_ort_dst "libonnxruntime.dylib")
    else()
        file(GLOB _ort_libs "${BSL_ORT_ROOT}/lib/libonnxruntime.so.*.*")
        set(_ort_dst "libonnxruntime.so")
    endif()
    list(GET _ort_libs 0 BSL_ORT_LIB_REAL)
    set(BSL_ORT_LIB_DST "${_ort_dst}")
endif()

set(BSL_MODELS_DIR "${CMAKE_SOURCE_DIR}/assets/models")
function(bsl_fetch_model fileName url sha256)
    if(EXISTS "${BSL_MODELS_DIR}/${fileName}")
        file(SHA256 "${BSL_MODELS_DIR}/${fileName}" _have)
        if(_have STREQUAL "${sha256}")
            return()
        endif()
        file(REMOVE "${BSL_MODELS_DIR}/${fileName}")
    endif()
    message(STATUS "Baixando modelo ${fileName}...")
    file(MAKE_DIRECTORY "${BSL_MODELS_DIR}")
    file(DOWNLOAD "${url}" "${BSL_MODELS_DIR}/${fileName}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON
        EXPECTED_HASH SHA256=${sha256})
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${BSL_MODELS_DIR}/${fileName}")
        message(WARNING "Falha ao baixar o modelo ${fileName}: ${_dl_status}")
    endif()
endfunction()
if(BSL_WITH_AI)
    set(_hf_depth "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/4472b7362082ad9968fee890ca0f1e5aca36b93d/onnx")
    set(_hf_modnet "https://huggingface.co/Xenova/modnet/resolve/fa2fa546052fba4c08921230a26cc69a333fca12/onnx")
    bsl_fetch_model("depth_anything_v2_small_fp16.onnx" "${_hf_depth}/model_fp16.onnx"
        "2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04")
    bsl_fetch_model("modnet_fp32.onnx" "${_hf_modnet}/model.onnx"
        "07c308cf0fc7e6e8b2065a12ed7fc07e1de8febb7dc7839d7b7f15dd66584df9")
endif()

add_library(stb_image STATIC third_party/stb_image_impl.c third_party/stb_image_write_impl.c
    third_party/stb_truetype_impl.c)
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})

if(NOT WIN32)
    add_library(bsl_miniaudio STATIC src/audio/MiniaudioImpl.cpp)
    target_include_directories(bsl_miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
    target_compile_definitions(bsl_miniaudio PUBLIC MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_ENGINE
        MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH)
    target_link_libraries(bsl_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    if(NOT APPLE)
        target_link_libraries(bsl_miniaudio PUBLIC m)
    endif()
endif()

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/src/ui)
target_compile_definitions(imgui PUBLIC "IMGUI_USER_CONFIG=\"BslImConfig.h\"")

if(BSL_WITH_SPOUT)
    set(BSL_SPOUTGL_DIR "${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL")
    add_library(spoutgl STATIC
        ${BSL_SPOUTGL_DIR}/Spout.cpp
        ${BSL_SPOUTGL_DIR}/SpoutCopy.cpp
        ${BSL_SPOUTGL_DIR}/SpoutDirectX.cpp
        ${BSL_SPOUTGL_DIR}/SpoutFrameCount.cpp
        ${BSL_SPOUTGL_DIR}/SpoutGL.cpp
        ${BSL_SPOUTGL_DIR}/SpoutGLextensions.cpp
        ${BSL_SPOUTGL_DIR}/SpoutReceiver.cpp
        ${BSL_SPOUTGL_DIR}/SpoutSender.cpp
        ${BSL_SPOUTGL_DIR}/SpoutSenderNames.cpp
        ${BSL_SPOUTGL_DIR}/SpoutSharedMemory.cpp
        ${BSL_SPOUTGL_DIR}/SpoutUtils.cpp)
    target_include_directories(spoutgl SYSTEM PUBLIC ${BSL_SPOUTGL_DIR})
    target_link_libraries(spoutgl PUBLIC opengl32 d3d11 d3d9 dxgi winmm version comctl32 shell32 ole32 oleaut32 uuid advapi32 user32 gdi32)
endif()

add_library(bslplatform STATIC
    src/platform/Paths.cpp
    src/platform/Utf.cpp
    src/platform/DynLib.cpp
    src/platform/ExePath.cpp
    src/platform/OpenUrl.cpp
    src/platform/Socket.cpp)
target_include_directories(bslplatform PUBLIC src)
target_compile_definitions(bslplatform PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
if(WIN32)
    target_link_libraries(bslplatform PUBLIC shell32 ole32 ws2_32 iphlpapi)
else()
    target_link_libraries(bslplatform PUBLIC ${CMAKE_DL_LIBS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(bslplatform PUBLIC rt)
    endif()
endif()

if(BSL_WITH_FFGL)
    set(BSL_FFGL_PLUGIN_SRC src/ffgl/FfglPe.cpp src/ffgl/FfglCall.cpp src/ffgl/FfglPlugin.cpp)
    set(BSL_FFGL_CATALOG_SRC src/ffgl/FfglCatalog.cpp)
else()
    set(BSL_FFGL_PLUGIN_SRC src/ffgl/FfglPlugin_stub.cpp)
    set(BSL_FFGL_CATALOG_SRC src/ffgl/FfglCatalog_stub.cpp)
endif()
add_library(bslffgl_core STATIC
    src/ffgl/FfglInfo.cpp
    ${BSL_FFGL_PLUGIN_SRC}
    src/ffgl/FfglParams.cpp)
target_include_directories(bslffgl_core PUBLIC src)
target_include_directories(bslffgl_core SYSTEM PUBLIC third_party/ffgl)
target_compile_definitions(bslffgl_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslffgl_core PUBLIC glad nlohmann_json::nlohmann_json)

if(WIN32)
    set(BSL_DOCTHUMB_SRC src/media/DocThumb_win.cpp)
else()
    set(BSL_DOCTHUMB_SRC src/media/DocThumb_posix.cpp)
endif()
add_library(bslmedia_cpu STATIC
    src/media/MediaFormats.cpp
    src/media/DocInfo.cpp
    ${BSL_DOCTHUMB_SRC}
    src/media/FrameConverter.cpp
    src/media/VideoDecoder.cpp
    src/media/NetStream.cpp
    src/media/ImageDecoder.cpp
    src/media/Thumbnailer.cpp)
target_include_directories(bslmedia_cpu PUBLIC src)
target_link_libraries(bslmedia_cpu PUBLIC bslplatform ffmpeg::ffmpeg stb_image nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(bslmedia_cpu PUBLIC ws2_32 iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(bslmedia_cpu PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(bslmedia_cpu PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(bslupdate_core STATIC
    src/update/SemVer.cpp
    src/update/Release.cpp
    src/update/Policy.cpp
    src/update/Sha256.cpp
    src/update/Http.cpp
    src/update/Zip.cpp
    src/update/Apply.cpp
    src/update/Service.cpp
    src/update/Markdown.cpp
    src/update/IssueReport.cpp
    src/update/SystemProbe.cpp)
target_include_directories(bslupdate_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(bslupdate_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslupdate_core PUBLIC bslplatform nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(bslupdate_core PUBLIC shell32 ole32)
    if(BSL_WITH_UPDATER)
        target_link_libraries(bslupdate_core PUBLIC winhttp)
    endif()
endif()

function(bsl_pick_impl outVar enabled)
    set(_list "")
    foreach(_base IN LISTS ARGN)
        if(enabled)
            list(APPEND _list "${_base}_win.cpp")
        else()
            list(APPEND _list "${_base}_stub.cpp")
        endif()
    endforeach()
    set(${outVar} ${_list} PARENT_SCOPE)
endfunction()
bsl_pick_impl(BSL_SPOUT_IN_SRC "${BSL_WITH_SPOUT}" src/media/SpoutInput)
bsl_pick_impl(BSL_SPOUT_OUT_SRC "${BSL_WITH_SPOUT}" src/output/SpoutOutput)
bsl_pick_impl(BSL_HOTKEYS_SRC "${BSL_WITH_HOTKEYS}" src/control/SystemHotkeys)
if(WIN32)
    set(BSL_AUDIO_SRC src/audio/AudioAnalyzer_win.cpp)
    set(BSL_DEVICE_SRC src/media/DeviceCatalog_win.cpp)
else()
    set(BSL_AUDIO_SRC src/audio/AudioAnalyzer_posix.cpp)
    set(BSL_DEVICE_SRC src/media/DeviceCatalog_posix.cpp)
endif()
bsl_pick_impl(BSL_SCREENCAP_SRC "${BSL_WITH_SCREENCAP}" src/media/ScreenCaptureSource src/media/CaptureTargets)
if(BSL_WITH_AI)
    set(BSL_AI_SRC src/ai/AiRuntime.cpp)
    bsl_pick_impl(_bsl_dml "${WIN32}" src/ai/AiDml)
    list(APPEND BSL_AI_SRC ${_bsl_dml})
else()
    set(BSL_AI_SRC src/ai/AiRuntime_stub.cpp)
endif()
bsl_pick_impl(BSL_D3D_SRC "${BSL_WITH_D3D}" src/render/D3D11Interop)
if(WIN32)
    set(BSL_OFFICE_SRC src/worker/OfficeConverter_win.cpp)
    set(BSL_WORKERHOST_SRC src/media/WorkerHost_win.cpp)
else()
    set(BSL_OFFICE_SRC src/worker/OfficeConverter_posix.cpp)
    set(BSL_WORKERHOST_SRC src/media/WorkerHost_posix.cpp)
endif()
set(BSL_HAVE_NFD OFF)
if(WIN32)
    set(BSL_DIALOGS_SRC src/io/FileDialogs_win.cpp)
else()
    set(BSL_DIALOGS_SRC src/io/FileDialogs_stub.cpp)
    set(_bsl_nfd_portal OFF)
    if(APPLE)
        set(BSL_HAVE_NFD ON)
    else()
        pkg_check_modules(BSL_DBUS QUIET IMPORTED_TARGET dbus-1)
        pkg_check_modules(BSL_GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
        if(BSL_DBUS_FOUND)
            set(BSL_HAVE_NFD ON)
            set(_bsl_nfd_portal ON)
        elseif(BSL_GTK3_FOUND)
            set(BSL_HAVE_NFD ON)
        else()
            message(WARNING "Dialogos de arquivo desativados no Linux. Instale com: sudo apt install libdbus-1-dev (ou libgtk-3-dev) e reconfigure.")
        endif()
    endif()
    if(BSL_HAVE_NFD)
        set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
        set(NFD_PORTAL ${_bsl_nfd_portal} CACHE BOOL "" FORCE)
        FetchContent_Declare(nfd
            GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
            GIT_TAG v1.2.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(nfd)
        set(BSL_DIALOGS_SRC src/io/FileDialogs_nfd.cpp)
    endif()
endif()
bsl_pick_impl(BSL_CONTROLPANEL_SRC "${WIN32}" src/media/ControlPanel)

add_library(bslsources STATIC
    src/app/Log.cpp
    src/app/Paths.cpp
    src/core/Generators.cpp
    src/media/YuvConverter.cpp
    src/media/VideoSource.cpp
    src/media/ImageSource.cpp
    src/media/FontCatalog.cpp
    src/media/TextRaster.cpp
    src/media/TextSource.cpp
    src/media/CaptureTargets.cpp
    ${BSL_SCREENCAP_SRC}
    src/media/DeviceCatalog.cpp
    ${BSL_DEVICE_SRC}
    src/media/CaptureDeviceSource.cpp
    ${BSL_WORKERHOST_SRC}
    src/media/RemoteSource.cpp
    src/media/FadeBlender.cpp
    src/media/PdfSource.cpp
    src/media/WebSource.cpp
    ${BSL_CONTROLPANEL_SRC}
    src/control/Keys.cpp
    src/control/ActionRegistry.cpp
    src/control/KeyBinding.cpp
    src/control/KeyDispatcher.cpp
    src/control/KeyService.cpp
    ${BSL_HOTKEYS_SRC}
    src/dmx/DmxProtocol.cpp
    src/dmx/DmxMerge.cpp
    src/dmx/DmxNet.cpp
    src/dmx/DmxMap.cpp
    src/dmx/DmxEngine.cpp
    src/dmx/DmxService.cpp
    src/dmx/DmxExport.cpp
    src/ndi/NdiRuntime.cpp
    src/ndi/NdiFinder.cpp
    src/media/NdiSource.cpp
    src/media/NetStreamSource.cpp
    ${BSL_SPOUT_IN_SRC}
    src/media/SpoutSource.cpp
    src/render/Shader.cpp
    src/render/Fbo.cpp
    src/render/FboPool.cpp
    src/render/TextureLoader.cpp
    src/render/TestPatternSource.cpp
    src/render/SolidColorSource.cpp
    ${BSL_D3D_SRC}
    src/render/ZoneMasks.cpp
    ${BSL_AI_SRC}
    src/ai/AiEngine.cpp
    src/fx/EffectChain.cpp
    src/fx/EffectRegistry.cpp
    src/fx/ChainRunner.cpp
    src/fx/FxGl.cpp
    src/fx/NativeEffects.cpp
    src/fx/IsfDoc.cpp
    src/fx/IsfRenderer.cpp
    src/fx/IsfLibrary.cpp
    src/fx/IsfReload.cpp
    src/fx/IsfEffect.cpp
    src/fx/IsfSource.cpp
    ${BSL_FFGL_CATALOG_SRC}
    src/ffgl/FfglInstance.cpp
    src/ffgl/FfglSource.cpp
    src/ffgl/FfglProvider.cpp
    src/ffgl/FfglImport.cpp
    src/plugin/PluginTemplates.cpp
    src/plugin/PluginFiles.cpp
    src/plugin/PluginPackage.cpp
    src/plugin/IsfCheck.cpp
    src/audio/AudioDsp.cpp
    ${BSL_AUDIO_SRC}
    src/fx/bsl/BslCommon.cpp
    src/fx/bsl/BslProvider.cpp
    src/fx/bsl/Vhs.cpp
    src/fx/bsl/Relight25D.cpp
    src/fx/bsl/BodyGlitch.cpp
    src/fx/bsl/PerformerAura.cpp
    src/fx/bsl/ZSlice.cpp
    src/fx/bsl/SliceRefiner2.cpp
    src/fx/bsl/SliceFeed.cpp
    src/fx/bsl/SequenceFeed.cpp
    src/fx/bsl/SliceInstancer.cpp
    src/fx/bsl/SeqDesenho.cpp
    src/fx/bsl/EffectSource.cpp
    src/fx/bsl/SeqSequencia.cpp
    src/fx/bsl/SeqDisparo.cpp
    src/fx/bsl/SeqAtraso.cpp
    src/fx/bsl/SeqAleatorios.cpp
    src/fx/bsl/SeqContorno.cpp
    src/fx/bsl/SeqLadrilhos.cpp
    src/fx/bsl/SeqAmostrador.cpp
    src/fx/bsl/SeqVarredura.cpp
    src/fx/bsl/SeqBordaNeon.cpp
    src/output/PixelMap.cpp
    src/output/NovaScr.cpp
    src/output/PixelMapMapping.cpp
    src/output/PatternRaster.cpp
    src/fx/bsl/PixelMapSource.cpp
    src/net/MiniHttp.cpp
    src/fx/bsl/SliceState.cpp
    src/fx/bsl/BslPreview.cpp
    src/fx/bsl/SliceRefiner3.cpp
    src/fx/bsl/WebStudio.cpp
    src/fx/bsl/BslWebSources.cpp
    src/seq/SeqModel.cpp
    src/seq/SeqJson.cpp
    src/seq/Envelope.cpp
    src/seq/EnvelopeStore.cpp
    src/seq/SeqFactory.cpp
    src/seq/SeqEdit.cpp
    src/seq/SeqHistory.cpp
    src/seq/SeqTiming.cpp
    src/seq/SeqView.cpp)
target_include_directories(bslsources PUBLIC src)
if(BSL_ORT_INCLUDE)
    target_include_directories(bslsources SYSTEM PRIVATE "${BSL_ORT_INCLUDE}")
endif()
if(BSL_DML_INCLUDE)
    target_include_directories(bslsources SYSTEM PRIVATE "${BSL_DML_INCLUDE}")
endif()
target_include_directories(bslsources PUBLIC third_party/ndi-min)
target_compile_definitions(bslsources PUBLIC GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslsources PUBLIC glad bslffgl_core bslmedia_cpu bslupdate_core nlohmann_json::nlohmann_json
    ${BSL_SPOUT_LIB})
if(WIN32)
    target_link_libraries(bslsources PUBLIC opengl32 dwmapi shell32 ole32 uuid ws2_32 iphlpapi)
    if(BSL_WITH_D3D)
        target_link_libraries(bslsources PUBLIC d3d11 d3d12 dxgi)
    endif()
    if(BSL_WITH_SCREENCAP)
        target_link_libraries(bslsources PUBLIC windowsapp runtimeobject)
    endif()
else()
    target_link_libraries(bslsources PUBLIC bsl_miniaudio Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(BSL_WITH_ATIVACAO)
    set(BSL_ATIVACAO_SRC src/ui/AtivacaoUi.cpp)
else()
    set(BSL_ATIVACAO_SRC "")
endif()
add_executable(BSLStage
    src/main.cpp
    src/app/CrashDump.cpp
    src/app/App.cpp
    src/app/FrameWatch.cpp
    src/app/PerformanceProfile.cpp
    src/app/Qa.cpp
    src/app/ProjectSession.cpp
    src/app/Settings.cpp
    src/app/IsfSetup.cpp
    src/legal/Notices.cpp
    src/app/FfglSetup.cpp
    src/core/Clip.cpp
    src/core/Composition.cpp
    src/core/TransformHistory.cpp
    ${BSL_DIALOGS_SRC}
    src/io/ProjectFile.cpp
    src/output/OutputSetup.cpp
    src/output/Warp.cpp
    src/output/Snap.cpp
    src/output/MapEdit.cpp
    src/output/MapHistory.cpp
    ${BSL_SPOUT_OUT_SRC}
    src/output/NdiOutput.cpp
    src/output/TextLabel.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp
    src/render/Compositor.cpp
    src/render/LiveThumb.cpp
    src/render/ThumbnailCache.cpp
    src/ui/Brand.cpp
    ${BSL_ATIVACAO_SRC}
    src/ui/Theme.cpp
    src/ui/Ui.cpp
    src/ui/LayoutPreset.cpp
    src/ui/ViewState.cpp
    src/ui/ViewUi.cpp
    src/ui/LayoutUi.cpp
    src/ui/KeysUi.cpp
    src/ui/Widgets.cpp
    src/ui/panels/BrowserPanel.cpp
    src/ui/panels/CompositionPanel.cpp
    src/ui/panels/TransportBar.cpp
    src/ui/panels/ClipPanel.cpp
    src/ui/panels/TabPanels.cpp
    src/ui/panels/EffectsPanel.cpp
    src/ui/panels/EffectChainWidget.cpp
    src/ui/panels/ZoneRouting.cpp
    src/ui/panels/PluginsWindow.cpp
    src/ui/panels/PluginWizard.cpp
    src/ui/panels/AudioWindow.cpp
    src/ui/panels/DmxWindow.cpp
    src/ui/panels/AiWindow.cpp
    src/ui/panels/PreviewPanel.cpp
    src/ui/panels/TransformEditor.cpp
    src/ui/panels/StatusBar.cpp
    src/ui/panels/MappingPanels.cpp
    src/ui/panels/MapView.cpp
    src/ui/panels/PixelMapPanels.cpp
    src/ui/panels/SequencePanels.cpp
    src/ui/panels/SeqCanvas.cpp
    src/ui/panels/SeqFactoryPanel.cpp
    src/ui/panels/EnvelopeEditor.cpp
    src/ui/panels/ParamWidgets.cpp
    src/ui/panels/SliceEditor.cpp
    src/ui/panels/CaptureTargetChooser.cpp
    src/ui/panels/NetStreamWindow.cpp
    src/ui/panels/VideoCompat.cpp
    src/ui/panels/UpdateUi.cpp
    src/ui/panels/ReportWindow.cpp
    src/ui/panels/LegalWindow.cpp)

target_include_directories(BSLStage PRIVATE src)
target_compile_definitions(BSLStage PRIVATE GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(BSLStage PRIVATE bsllicense bslsources imgui glfw nlohmann_json::nlohmann_json bslmedia_cpu ${BSL_SPOUT_LIB})
if(WIN32)
    target_link_libraries(BSLStage PRIVATE shell32 ole32 uuid winmm psapi dbghelp)
endif()
if(BSL_HAVE_NFD)
    target_link_libraries(BSLStage PRIVATE nfd)
endif()
if(TARGET bslstage_worker)
    add_dependencies(BSLStage bslstage_worker)
endif()
if(BSL_WITH_UPDATER AND WIN32)
    add_dependencies(BSLStage bslstage_updater)
endif()
bsl_add_version_resource(BSLStage "BSL Stage" BSLStage)

if(BSL_WITH_UPDATER AND WIN32)
    add_executable(bslstage_updater WIN32 src/update/updater_main.cpp)
    target_link_libraries(bslstage_updater PRIVATE bslupdate_core comctl32)
    target_link_options(bslstage_updater PRIVATE /ENTRY:wWinMainCRTStartup)
    bsl_add_version_resource(bslstage_updater "BSL Stage - Atualizador" bslstage_updater)
endif()

if(BSL_HAVE_CEF)
    message(STATUS "CEF encontrado em ${BSL_CEF_DIR}: a fonte Web sera compilada")
    file(GLOB_RECURSE _cef_wrapper_src CONFIGURE_DEPENDS "${BSL_CEF_DIR}/libcef_dll/*.cc")
    add_library(libcef_dll_wrapper STATIC ${_cef_wrapper_src})
    target_include_directories(libcef_dll_wrapper PUBLIC "${BSL_CEF_DIR}")
    target_compile_definitions(libcef_dll_wrapper PRIVATE WRAPPING_CEF_SHARED NOMINMAX WIN32_LEAN_AND_MEAN
        __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(libcef_dll_wrapper PRIVATE /w /utf-8)
    endif()
    set_target_properties(libcef_dll_wrapper PROPERTIES FOLDER third_party)
elseif(BSL_WITH_CEF)
    message(WARNING "CEF nao encontrado (BSL_CEF_DIR=\"${BSL_CEF_DIR}\"): a fonte Web fica desabilitada.")
endif()

set(BSL_WORKER_SOURCES
    src/worker/WorkerMain.cpp
    src/worker/WorkerUtil.cpp
    src/worker/PdfDocument.cpp
    ${BSL_OFFICE_SRC})
if(WIN32)
    list(APPEND BSL_WORKER_SOURCES src/worker/PipeChannel_win.cpp src/worker/FrameSink_win.cpp src/worker/FfglScan.cpp)
else()
    list(APPEND BSL_WORKER_SOURCES src/worker/PipeChannel_posix.cpp src/worker/FrameSink_posix.cpp)
endif()
add_executable(bslstage_worker WIN32 ${BSL_WORKER_SOURCES})
bsl_add_version_resource(bslstage_worker "BSL Stage - Processo auxiliar" bslstage_worker)
target_include_directories(bslstage_worker PRIVATE src)
target_include_directories(bslstage_worker SYSTEM PRIVATE "${BSL_PDFIUM_ROOT}/include")
target_compile_definitions(bslstage_worker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslstage_worker PRIVATE bslplatform nlohmann_json::nlohmann_json "${BSL_PDFIUM_LIB}")
if(WIN32)
    target_link_libraries(bslstage_worker PRIVATE bslffgl_core ole32 oleaut32 uuid shell32 advapi32 user32)
    if(BSL_WITH_D3D)
        target_link_libraries(bslstage_worker PRIVATE d3d11 dxgi d3dcompiler)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(bslstage_worker PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(bslstage_worker PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
add_custom_command(TARGET bslstage_worker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BSL_PDFIUM_RUNTIME}" $<TARGET_FILE_DIR:bslstage_worker>
    VERBATIM)

if(BSL_HAVE_CEF AND WIN32)
    target_sources(bslstage_worker PRIVATE src/worker/WebSession.cpp src/worker/PanelWindow.cpp)
    target_include_directories(bslstage_worker SYSTEM PRIVATE "${BSL_CEF_DIR}")
    target_link_libraries(bslstage_worker PRIVATE libcef_dll_wrapper "${BSL_CEF_DIR}/Release/libcef.lib" delayimp)
    target_link_options(bslstage_worker PRIVATE /DELAYLOAD:libcef.dll)
    file(GLOB _cef_rt "${BSL_CEF_DIR}/Release/*")
    list(FILTER _cef_rt EXCLUDE REGEX "(bootstrap|bootstrapc)\.exe$|\.lib$")
    file(GLOB _cef_res "${BSL_CEF_DIR}/Resources/*")
    list(FILTER _cef_res EXCLUDE REGEX "/locales$")
    add_custom_command(TARGET bslstage_worker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:bslstage_worker>/cef"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cef_rt} ${_cef_res} "$<TARGET_FILE_DIR:bslstage_worker>/cef"
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${BSL_CEF_DIR}/Resources/locales"
                "$<TARGET_FILE_DIR:bslstage_worker>/cef/locales"
        VERBATIM)
endif()

add_executable(bslstage_platformtest tools/platformtest.cpp)
target_link_libraries(bslstage_platformtest PRIVATE bslplatform)

add_executable(bslstage_mediatest tools/mediatest.cpp)
target_link_libraries(bslstage_mediatest PRIVATE bslmedia_cpu)

add_executable(bslstage_gltest tools/gltest.cpp)
target_link_libraries(bslstage_gltest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_streamtest tools/streamtest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputSetup.cpp)
target_link_libraries(bslstage_streamtest PRIVATE bslsources glfw nlohmann_json::nlohmann_json bslmedia_cpu ${BSL_SHELL_LIBS})

add_executable(bslstage_livetest tools/livetest.cpp)
target_link_libraries(bslstage_livetest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_fxtest tools/fxtest.cpp
    src/render/Compositor.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_fxtest PRIVATE src)
target_link_libraries(bslstage_fxtest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_isfcheck tools/isfcheck.cpp)
target_include_directories(bslstage_isfcheck PRIVATE src)
target_compile_definitions(bslstage_isfcheck PRIVATE NOMINMAX)
target_link_libraries(bslstage_isfcheck PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_plugintest tools/plugintest.cpp)
target_include_directories(bslstage_plugintest PRIVATE src)
target_compile_definitions(bslstage_plugintest PRIVATE NOMINMAX BSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(bslstage_plugintest PRIVATE bslsources glfw bslmedia_cpu)
add_dependencies(bslstage_plugintest bslstage_isfcheck)

if(BSL_WITH_FFGL AND WIN32)
    set(_fftp tools/ffgl-test-plugins)
    file(GLOB _fftp_sdk "${_fftp}/sdk/ffgl/*.cpp" "${_fftp}/sdk/ffglex/*.cpp")
    function(bsl_test_plugin target outName)
        add_library(${target} SHARED ${ARGN})
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        target_link_libraries(${target} PRIVATE glad opengl32)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME ${outName} FOLDER tests
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/ffgl-test-plugins")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W0 /utf-8)
        endif()
    endfunction()
    bsl_test_plugin(ffgltest_addsubtract "TestAddSubtract" ${_fftp_sdk} ${_fftp}/AddSubtract/AddSubtract.cpp ${_fftp}/shim/glew_shim.c)
    target_include_directories(ffgltest_addsubtract PRIVATE ${_fftp}/shim ${_fftp}/sdk)
    bsl_test_plugin(ffgltest_gradients "TestGradients" ${_fftp_sdk} ${_fftp}/Gradients/FFGLGradients.cpp ${_fftp}/shim/glew_shim.c)
    target_include_directories(ffgltest_gradients PRIVATE ${_fftp}/shim ${_fftp}/sdk)
    bsl_test_plugin(ffgltest_probe_fx "TestProbeFx" ${_fftp_sdk} ${_fftp}/ProbePlugin.cpp ${_fftp}/shim/glew_shim.c)
    target_include_directories(ffgltest_probe_fx PRIVATE ${_fftp}/shim ${_fftp}/sdk)
    bsl_test_plugin(ffgltest_probe_src "TestProbeSrc" ${_fftp_sdk} ${_fftp}/ProbePlugin.cpp ${_fftp}/shim/glew_shim.c)
    target_include_directories(ffgltest_probe_src PRIVATE ${_fftp}/shim ${_fftp}/sdk)
    target_compile_definitions(ffgltest_probe_src PRIVATE PROBE_SOURCE)
    bsl_test_plugin(ffgltest_crash "TestCrash" ${_fftp}/FailPlugin.cpp)
    target_compile_definitions(ffgltest_crash PRIVATE FAIL_CRASH)
    target_include_directories(ffgltest_crash SYSTEM PRIVATE third_party/ffgl)
    bsl_test_plugin(ffgltest_hang "TestHang" ${_fftp}/FailPlugin.cpp)
    target_compile_definitions(ffgltest_hang PRIVATE FAIL_HANG)
    target_include_directories(ffgltest_hang SYSTEM PRIVATE third_party/ffgl)
    bsl_test_plugin(ffgltest_nomain "TestNoMain" ${_fftp}/FailPlugin.cpp)
    target_compile_definitions(ffgltest_nomain PRIVATE FAIL_NOMAIN)

    add_subdirectory(sdk/ffgl-template)
    set_target_properties(bsl_ffgl_example PROPERTIES FOLDER tests
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/ffgl-test-plugins")

    add_executable(bslstage_ffgltest tools/ffgltest.cpp)
    target_include_directories(bslstage_ffgltest PRIVATE src)
    target_link_libraries(bslstage_ffgltest PRIVATE bslsources glfw bslmedia_cpu)
    add_dependencies(bslstage_ffgltest bslstage_worker ffgltest_probe_fx ffgltest_probe_src ffgltest_addsubtract ffgltest_gradients
        ffgltest_crash ffgltest_hang ffgltest_nomain bsl_ffgl_example)
endif()

add_executable(bslstage_bsltest tools/bsltest.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp src/output/NdiOutput.cpp
    ${BSL_SPOUT_OUT_SRC} src/render/Compositor.cpp)
target_include_directories(bslstage_bsltest PRIVATE src)
target_link_libraries(bslstage_bsltest PRIVATE bslsources glfw bslmedia_cpu ${BSL_SPOUT_LIB})
if(TARGET bslstage_worker)
    add_dependencies(bslstage_bsltest bslstage_worker)
endif()

add_executable(bslstage_outputstress tools/outputstress.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp src/output/NdiOutput.cpp
    ${BSL_SPOUT_OUT_SRC} src/render/Compositor.cpp)
target_include_directories(bslstage_outputstress PRIVATE src)
target_link_libraries(bslstage_outputstress PRIVATE bslsources glfw bslmedia_cpu ${BSL_SPOUT_LIB})
if(WIN32)
    target_link_libraries(bslstage_outputstress PRIVATE psapi)
endif()
if(TARGET bslstage_worker)
    add_dependencies(bslstage_outputstress bslstage_worker)
endif()

add_executable(bslstage_transformtest tools/transformtest.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/core/TransformHistory.cpp src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp
    src/output/NdiOutput.cpp ${BSL_SPOUT_OUT_SRC} src/render/Compositor.cpp)
target_include_directories(bslstage_transformtest PRIVATE src)
target_link_libraries(bslstage_transformtest PRIVATE bslsources glfw bslmedia_cpu ${BSL_SPOUT_LIB})
if(TARGET bslstage_worker)
    add_dependencies(bslstage_transformtest bslstage_worker)
endif()

add_executable(bslstage_aitest tools/aitest.cpp)
target_include_directories(bslstage_aitest PRIVATE src)
target_link_libraries(bslstage_aitest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_doctest tools/doctest.cpp ${BSL_OFFICE_SRC} src/worker/WorkerUtil.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_doctest PRIVATE src)
target_link_libraries(bslstage_doctest PRIVATE bslsources glfw bslmedia_cpu)
if(WIN32)
    target_link_libraries(bslstage_doctest PRIVATE ole32 oleaut32 uuid)
endif()
if(TARGET bslstage_worker)
    add_dependencies(bslstage_doctest bslstage_worker)
endif()

if(BSL_WITH_SPOUT)
    add_executable(bslstage_spouttest tools/spouttest.cpp tools/spouttest_source.cpp ${BSL_SPOUT_OUT_SRC})
    target_include_directories(bslstage_spouttest PRIVATE src)
    target_compile_definitions(bslstage_spouttest PRIVATE NOMINMAX)
    target_link_libraries(bslstage_spouttest PRIVATE bslsources glfw bslmedia_cpu ${BSL_SPOUT_LIB})
endif()

add_executable(bslstage_ndittest tools/ndittest.cpp
    src/output/NdiOutput.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp
    src/output/OutputSetup.cpp
    src/output/Warp.cpp
    src/output/TextLabel.cpp
    ${BSL_SPOUT_OUT_SRC}
    src/render/Compositor.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_ndittest PRIVATE src)
target_link_libraries(bslstage_ndittest PRIVATE bslsources glfw bslmedia_cpu ${BSL_SPOUT_LIB})

add_executable(bslstage_projecttest tools/projecttest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputSetup.cpp)
target_link_libraries(bslstage_projecttest PRIVATE bslsources nlohmann_json::nlohmann_json bslmedia_cpu ${BSL_SHELL_LIBS})

add_executable(bslstage_keystest tools/keystest.cpp
    src/core/Clip.cpp src/core/Composition.cpp src/ui/ViewState.cpp)
target_include_directories(bslstage_keystest PRIVATE src)
target_link_libraries(bslstage_keystest PRIVATE bslsources glfw bslmedia_cpu nlohmann_json::nlohmann_json)

add_executable(bslstage_maptest tools/maptest.cpp
    src/output/Snap.cpp
    src/output/MapEdit.cpp
    src/output/MapHistory.cpp
    src/output/OutputSetup.cpp)
target_include_directories(bslstage_maptest PRIVATE src)

add_executable(bslstage_pixelmaptest tools/pixelmaptest.cpp src/output/OutputSetup.cpp)
target_include_directories(bslstage_pixelmaptest PRIVATE src)
target_compile_definitions(bslstage_pixelmaptest PRIVATE BSL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/fixtures")
target_link_libraries(bslstage_pixelmaptest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_dmxtest tools/dmxtest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp src/output/OutputSetup.cpp src/ui/ViewState.cpp)
target_include_directories(bslstage_dmxtest PRIVATE src)
target_link_libraries(bslstage_dmxtest PRIVATE bslsources glfw bslmedia_cpu nlohmann_json::nlohmann_json ${BSL_SHELL_LIBS})

add_executable(bslstage_uitest tools/uitest.cpp src/ui/LayoutPreset.cpp src/ui/ViewState.cpp
    src/legal/Notices.cpp src/app/Settings.cpp src/app/PerformanceProfile.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_uitest PRIVATE src)
target_compile_definitions(bslstage_uitest PRIVATE BSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(bslstage_uitest PRIVATE nlohmann_json::nlohmann_json bslsources glfw bslmedia_cpu)

add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(monocypher SYSTEM PUBLIC ${monocypher_SOURCE_DIR}/src)

add_library(bsllicense STATIC src/license/Licenca.cpp src/license/Ativacao.cpp)
target_include_directories(bsllicense PUBLIC src)
target_link_libraries(bsllicense PUBLIC monocypher)

add_executable(bslstage_ativacaotest tools/ativacaotest.cpp)
target_link_libraries(bslstage_ativacaotest PRIVATE bsllicense)

if(WIN32)
    add_executable(bslstage_keygen tools/licenca/keygen.cpp)
    target_compile_definitions(bslstage_keygen PRIVATE BSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(bslstage_keygen PRIVATE bsllicense bslplatform bcrypt)
endif()

if(WIN32 AND BSL_WITH_MSI)
    add_executable(bslstage_licencatest tools/licencatest.cpp)
    target_compile_definitions(bslstage_licencatest PRIVATE BSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}" NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(bslstage_licencatest PRIVATE bsllicense bcrypt msi)

    add_library(bslstage_licenca_ca SHARED src/license/CustomAction.cpp src/license/Licenca.cpp
        ${monocypher_SOURCE_DIR}/src/monocypher.c)
    target_include_directories(bslstage_licenca_ca PRIVATE src)
    target_include_directories(bslstage_licenca_ca SYSTEM PRIVATE ${monocypher_SOURCE_DIR}/src)
    target_compile_definitions(bslstage_licenca_ca PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(bslstage_licenca_ca PRIVATE msi)
    set_target_properties(bslstage_licenca_ca PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

if(BSL_WITH_UPDATER AND WIN32)
    add_executable(bslstage_updatetest tools/updatetest.cpp)
    target_link_libraries(bslstage_updatetest PRIVATE bslupdate_core)
    add_dependencies(bslstage_updatetest bslstage_updater)
    target_compile_definitions(bslstage_updatetest PRIVATE BSL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/fixtures")
endif()

add_executable(bslstage_seqtest src/output/OutputSetup.cpp tools/seqtest.cpp tools/seqtest_model.cpp tools/seqtest_env.cpp tools/seqtest_factory.cpp
    tools/seqtest_edit.cpp tools/seqtest_timing.cpp tools/seqtest_ui.cpp)
target_include_directories(bslstage_seqtest PRIVATE src)
target_link_libraries(bslstage_seqtest PRIVATE bslsources nlohmann_json::nlohmann_json ${BSL_SHELL_LIBS})

add_executable(bslstage_seqfxtest tools/seqfxtest.cpp tools/seqfx_base.cpp tools/seqfx_instanciador.cpp
    tools/seqfx_util.cpp tools/seqfx_sequencia.cpp tools/seqfx_disparo.cpp tools/seqfx_atraso.cpp
    tools/seqfx_aleatorios.cpp tools/seqfx_fonte.cpp tools/seqfx_contorno.cpp tools/seqfx_ladrilhos.cpp
    tools/seqfx_amostrador.cpp tools/seqfx_varredura.cpp tools/seqfx_bordaneon.cpp)
target_include_directories(bslstage_seqfxtest PRIVATE src)
target_link_libraries(bslstage_seqfxtest PRIVATE bslsources glfw bslmedia_cpu)

if(WIN32)
    foreach(_tgt BSLStage bslstage_spouttest bslstage_ndittest bslstage_pixelmaptest bslstage_mediatest bslstage_gltest bslstage_projecttest bslstage_livetest bslstage_doctest bslstage_fxtest bslstage_ffgltest bslstage_bsltest bslstage_outputstress bslstage_aitest bslstage_keystest bslstage_transformtest bslstage_dmxtest bslstage_isfcheck bslstage_plugintest bslstage_streamtest bslstage_seqfxtest bslstage_seqtest)
        if(TARGET ${_tgt})
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BSL_FFMPEG_DLLS} ${BSL_ORT_DLLS} $<TARGET_FILE_DIR:${_tgt}>
                VERBATIM)
        endif()
    endforeach()
endif()

if(NOT WIN32 AND BSL_WITH_AI AND BSL_ORT_LIB_REAL)
    foreach(_tgt BSLStage bslstage_aitest)
        if(TARGET ${_tgt})
            add_custom_command(TARGET ${_tgt} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BSL_ORT_LIB_REAL}" "$<TARGET_FILE_DIR:${_tgt}>/${BSL_ORT_LIB_DST}"
                VERBATIM)
            set_target_properties(${_tgt} PROPERTIES BUILD_RPATH "$ORIGIN;$ORIGIN/../lib" INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib")
        endif()
    endforeach()
endif()

add_custom_command(TARGET BSLStage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:BSLStage>/assets
    VERBATIM)

add_custom_target(bslstage_legal_docs ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        "${CMAKE_SOURCE_DIR}/PRIVACIDADE.md" "${CMAKE_SOURCE_DIR}/TERMOS-DE-USO.md" $<TARGET_FILE_DIR:BSLStage>
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_SOURCE_DIR}/licenses" $<TARGET_FILE_DIR:BSLStage>/licenses
    VERBATIM)
if(BSL_HAVE_CEF AND EXISTS "${BSL_CEF_DIR}/CREDITS.html")
    add_custom_command(TARGET bslstage_legal_docs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BSL_CEF_DIR}/CREDITS.html" $<TARGET_FILE_DIR:BSLStage>/licenses/cef-CREDITS.html
        VERBATIM)
endif()
add_dependencies(BSLStage bslstage_legal_docs)

if(MSVC)
    set(_bsl_w4_targets BSLStage
        bslmedia_cpu
        bslffgl_core
        bslsources
        bslstage_livetest
        bslstage_mediatest
        bslstage_gltest
        bslstage_spouttest
        bslstage_ndittest
        bslstage_projecttest
        bslplatform
        bslstage_platformtest
        bslstage_streamtest
        bslstage_maptest
        bslstage_uitest
        bslstage_pixelmaptest
        bslstage_doctest
        bslstage_fxtest
        bslstage_ffgltest
        bslstage_bsltest
        bslstage_outputstress
        bslstage_aitest
        bslstage_keystest
        bslstage_transformtest
        bslstage_dmxtest
        bslstage_isfcheck
        bslstage_plugintest
        bslupdate_core
        bslstage_updater
        bslstage_updatetest
        bsllicense
        bslstage_keygen
        bslstage_licencatest
        bslstage_ativacaotest
        bslstage_seqtest
        bslstage_seqfxtest
        bslstage_worker)
    foreach(_t IN LISTS _bsl_w4_targets)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE /W4 /utf-8 /permissive-)
        endif()
    endforeach()
    if(TARGET spoutgl)
        target_compile_options(spoutgl PRIVATE /utf-8 /w)
    endif()
    target_link_options(BSLStage PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>
        $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
    target_link_options(bslstage_worker PRIVATE /ENTRY:wWinMainCRTStartup)
    foreach(_t BSLStage bslsources bslmedia_cpu bslffgl_core imgui glfw)
        target_compile_options(${_t} PRIVATE $<$<CONFIG:Release>:/Zi>)
    endforeach()
    target_link_options(BSLStage PRIVATE $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT BSLStage)
