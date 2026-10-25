cmake_minimum_required(VERSION 3.25)
project(BSLStage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Dependencias antigas declaram cmake_minimum_required < 3.5 (removido no CMake 4)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# --- GLFW 3.4 ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

# --- Dear ImGui (branch docking) ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.93.0-docking
    GIT_SHALLOW TRUE)

# --- nlohmann/json 3.11.3 ---
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)

# --- stb (nothings/stb, commit fixo; so stb_image e usado) ---
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5.zip)

# --- Spout2 2.007.018 (BSD-2, release estavel de 10/2026). Usamos so o codigo-fonte do SpoutGL
# (SPOUTSDK/SpoutGL), compilado como biblioteca estatica propria: sem DLL extra para distribuir.
# SOURCE_SUBDIR inexistente impede o FetchContent de rodar o CMakeLists do Spout (que gera DLLs/exemplos).
FetchContent_Declare(spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG 2.007.018
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR bsl_sem_cmake_do_spout)

FetchContent_MakeAvailable(glfw imgui nlohmann_json stb spout2)

# --- FFmpeg (BtbN, LGPL shared, win64) baixado no configure para third_party/ffmpeg ---
# 9.x: o 8.x ainda nao decodifica WebP animado (demuxer/decoder webp_anim entrou no 9.0)
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

# --- PDFium (bblanchon/pdfium-binaries, BSD-3, win-x64, release estavel) baixado no configure para third_party/pdfium ---
# So o processo auxiliar (bslstage_worker.exe) usa o PDFium.
set(BSL_PDFIUM_TAG "8111")
set(BSL_PDFIUM_ROOT "${CMAKE_SOURCE_DIR}/third_party/pdfium/chromium-${BSL_PDFIUM_TAG}")
if(NOT EXISTS "${BSL_PDFIUM_ROOT}/include/fpdfview.h")
    set(_tgz "${CMAKE_SOURCE_DIR}/third_party/pdfium/pdfium-win-x64-${BSL_PDFIUM_TAG}.tgz")
    message(STATUS "Baixando PDFium chromium/${BSL_PDFIUM_TAG} (bblanchon/pdfium-binaries)...")
    file(MAKE_DIRECTORY "${BSL_PDFIUM_ROOT}")
    file(DOWNLOAD
        "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/${BSL_PDFIUM_TAG}/pdfium-win-x64.tgz"
        "${_tgz}" SHOW_PROGRESS STATUS _dl_status TLS_VERIFY ON)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${_tgz}")
        message(FATAL_ERROR "Falha ao baixar o PDFium: ${_dl_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_tgz}" DESTINATION "${BSL_PDFIUM_ROOT}")
    file(REMOVE "${_tgz}")
endif()

# stb_image / stb_truetype: implementacao compilada em C, sem os warnings do projeto
add_library(stb_image STATIC third_party/stb_image_impl.c third_party/stb_image_write_impl.c
    third_party/stb_truetype_impl.c)
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})

# --- glad (gerado com: glad --api gl:core=3.3 c) ---
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# --- ImGui + backends GLFW/OpenGL3 (sem warnings do projeto) ---
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
# Config propria do ImGui: hooks de itens (modo --qa) e IM_ASSERT que registra no log em vez de sumir em Release
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/src/ui)
target_compile_definitions(imgui PUBLIC "IMGUI_USER_CONFIG=\"BslImConfig.h\"")

# --- SpoutGL (Spout2 SDK) ---
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

# --- Midia: decodificacao em CPU (sem GL, testavel isoladamente) ---
add_library(bslmedia_cpu STATIC
    src/media/MediaFormats.cpp
    src/media/DocInfo.cpp
    src/media/DocThumb.cpp
    src/media/FrameConverter.cpp
    src/media/VideoDecoder.cpp
    src/media/ImageDecoder.cpp
    src/media/Thumbnailer.cpp)
target_include_directories(bslmedia_cpu PUBLIC src)
target_link_libraries(bslmedia_cpu PUBLIC ffmpeg::ffmpeg stb_image nlohmann_json::nlohmann_json)
target_compile_definitions(bslmedia_cpu PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

# --- Fontes de midia no lado GPU + geradores (texto, captura de tela, webcam...) e utilitarios base ---
# Biblioteca compartilhada pelo app e pelos testes (gltest, projecttest, livetest).
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
    src/media/ScreenCaptureSource.cpp
    src/media/DeviceCatalog.cpp
    src/media/CaptureDeviceSource.cpp
    src/media/WorkerHost.cpp
    src/media/RemoteSource.cpp
    src/media/FadeBlender.cpp
    src/media/PdfSource.cpp
    src/media/WebSource.cpp
    src/render/Shader.cpp
    src/render/Fbo.cpp
    src/render/TestPatternSource.cpp
    src/render/SolidColorSource.cpp
    src/render/D3D11Interop.cpp)
target_include_directories(bslsources PUBLIC src)
target_compile_definitions(bslsources PUBLIC GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslsources PUBLIC glad bslmedia_cpu nlohmann_json::nlohmann_json
    opengl32 d3d11 dxgi dwmapi windowsapp runtimeobject shell32 ole32 uuid)

# --- Executavel ---
add_executable(BSLStage
    src/main.cpp
    src/app/App.cpp
    src/app/PerformanceProfile.cpp
    src/app/Qa.cpp
    src/app/ProjectSession.cpp
    src/app/Settings.cpp
    src/core/Clip.cpp
    src/core/Composition.cpp
    src/io/FileDialogs.cpp
    src/io/ProjectFile.cpp
    src/output/OutputSetup.cpp
    src/output/Warp.cpp
    src/output/SpoutOutput.cpp
    src/output/TextLabel.cpp
    src/output/OutputRenderer.cpp
    src/render/Compositor.cpp
    src/render/LiveThumb.cpp
    src/render/TextureLoader.cpp
    src/render/ThumbnailCache.cpp
    src/ui/Brand.cpp
    src/ui/Theme.cpp
    src/ui/Ui.cpp
    src/ui/Widgets.cpp
    src/ui/panels/BrowserPanel.cpp
    src/ui/panels/CompositionPanel.cpp
    src/ui/panels/TransportBar.cpp
    src/ui/panels/ClipPanel.cpp
    src/ui/panels/TabPanels.cpp
    src/ui/panels/PreviewPanel.cpp
    src/ui/panels/StatusBar.cpp
    src/ui/panels/MappingPanels.cpp
    src/ui/panels/ParamWidgets.cpp
    src/ui/panels/CaptureTargetChooser.cpp)

target_include_directories(BSLStage PRIVATE src)
target_compile_definitions(BSLStage PRIVATE GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN
    BSL_VERSION="${PROJECT_VERSION}")
target_link_libraries(BSLStage PRIVATE bslsources imgui glfw nlohmann_json::nlohmann_json bslmedia_cpu spoutgl shell32 ole32 uuid winmm psapi)
add_dependencies(BSLStage bslstage_worker)

# --- CEF (Chromium Embedded Framework): Web off-screen no processo auxiliar ---
# Distribuicao binaria padrao (include/, libcef_dll/, Release/, Resources/). Se nao existir, a Web fica
# desabilitada (com aviso) e o resto compila normalmente.
set(BSL_CEF_DIR "C:/Users/GABRIEL - BSL LIGHT/Desktop/ffgl-master/deps/cef" CACHE PATH
    "Distribuicao binaria do CEF para a fonte Web. Vazio ou inexistente = Web desabilitada")
set(BSL_HAVE_CEF OFF)
if(BSL_CEF_DIR AND EXISTS "${BSL_CEF_DIR}/include/cef_version.h" AND EXISTS "${BSL_CEF_DIR}/Release/libcef.lib")
    set(BSL_HAVE_CEF ON)
    message(STATUS "CEF encontrado em ${BSL_CEF_DIR}: a fonte Web sera compilada")
    # libcef_dll_wrapper compilado do codigo-fonte (a distribuicao so traz libcef.dll/.lib)
    file(GLOB_RECURSE _cef_wrapper_src CONFIGURE_DEPENDS "${BSL_CEF_DIR}/libcef_dll/*.cc")
    add_library(libcef_dll_wrapper STATIC ${_cef_wrapper_src})
    target_include_directories(libcef_dll_wrapper PUBLIC "${BSL_CEF_DIR}")
    target_compile_definitions(libcef_dll_wrapper PRIVATE WRAPPING_CEF_SHARED NOMINMAX WIN32_LEAN_AND_MEAN
        __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(libcef_dll_wrapper PRIVATE /w /utf-8) # codigo de terceiros: sem warnings
    endif()
    set_target_properties(libcef_dll_wrapper PROPERTIES FOLDER third_party)
else()
    message(WARNING "CEF nao encontrado (BSL_CEF_DIR=\"${BSL_CEF_DIR}\"): a fonte Web fica desabilitada.")
endif()

# --- Processo auxiliar (PDF, Office e Web): um por clip; se travar, o show continua ---
set(BSL_WORKER_SOURCES
    src/worker/WorkerMain.cpp
    src/worker/WorkerUtil.cpp
    src/worker/PipeChannel.cpp
    src/worker/FrameSink.cpp
    src/worker/PdfDocument.cpp
    src/worker/OfficeConverter.cpp)
add_executable(bslstage_worker WIN32 ${BSL_WORKER_SOURCES})
target_include_directories(bslstage_worker PRIVATE src)
target_include_directories(bslstage_worker SYSTEM PRIVATE "${BSL_PDFIUM_ROOT}/include")
target_compile_definitions(bslstage_worker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslstage_worker PRIVATE nlohmann_json::nlohmann_json "${BSL_PDFIUM_ROOT}/lib/pdfium.dll.lib"
    d3d11 dxgi d3dcompiler ole32 oleaut32 uuid shell32 advapi32 user32)
add_custom_command(TARGET bslstage_worker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BSL_PDFIUM_ROOT}/bin/pdfium.dll" $<TARGET_FILE_DIR:bslstage_worker>
    VERBATIM)

if(BSL_HAVE_CEF)
    target_sources(bslstage_worker PRIVATE src/worker/WebSession.cpp)
    target_compile_definitions(bslstage_worker PRIVATE BSL_WITH_CEF)
    target_include_directories(bslstage_worker SYSTEM PRIVATE "${BSL_CEF_DIR}")
    target_link_libraries(bslstage_worker PRIVATE libcef_dll_wrapper "${BSL_CEF_DIR}/Release/libcef.lib" delayimp)
    # libcef.dll so carrega quando a Web e usada (e fica em cef/, ao lado do worker)
    target_link_options(bslstage_worker PRIVATE /DELAYLOAD:libcef.dll)
    # Runtime + Resources juntos em <pasta do exe>/cef (resources_dir_path e locales_dir_path apontam para la)
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

# --- Teste de midia (console, sem GL) ---
add_executable(bslstage_mediatest tools/mediatest.cpp)
target_link_libraries(bslstage_mediatest PRIVATE bslmedia_cpu)

# --- Smoke test do lado GPU (janela oculta, precisa de GL 3.3) ---
add_executable(bslstage_gltest tools/gltest.cpp)
target_link_libraries(bslstage_gltest PRIVATE bslsources glfw bslmedia_cpu)

# --- Teste das fontes ao vivo e geradores (janela oculta): texto, letreiro, captura de tela, webcam ---
add_executable(bslstage_livetest tools/livetest.cpp)
target_link_libraries(bslstage_livetest PRIVATE bslsources glfw bslmedia_cpu)

# --- Teste de documentos e Web (janela oculta): PDF, Office, Web, worker que cai ---
add_executable(bslstage_doctest tools/doctest.cpp src/worker/OfficeConverter.cpp src/worker/WorkerUtil.cpp)
target_include_directories(bslstage_doctest PRIVATE src)
target_link_libraries(bslstage_doctest PRIVATE bslsources glfw bslmedia_cpu ole32 oleaut32 uuid)
add_dependencies(bslstage_doctest bslstage_worker)

# --- Teste do Spout: sender (bsl::SpoutOutput) -> receiver no mesmo processo, confere pixels ---
add_executable(bslstage_spouttest tools/spouttest.cpp src/output/SpoutOutput.cpp)
target_include_directories(bslstage_spouttest PRIVATE src)
target_compile_definitions(bslstage_spouttest PRIVATE NOMINMAX)
target_link_libraries(bslstage_spouttest PRIVATE glfw spoutgl)

# --- Teste de projeto (.bslstage): console, sem janela nem contexto GL (so linka as fontes que a Composition referencia) ---
add_executable(bslstage_projecttest tools/projecttest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputSetup.cpp)
target_link_libraries(bslstage_projecttest PRIVATE bslsources nlohmann_json::nlohmann_json bslmedia_cpu shell32 ole32)

foreach(_tgt BSLStage bslstage_mediatest bslstage_gltest bslstage_projecttest bslstage_livetest bslstage_doctest)
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BSL_FFMPEG_DLLS} $<TARGET_FILE_DIR:${_tgt}>
        VERBATIM)
endforeach()

# Marca, fontes e demais recursos ficam ao lado do exe (assetsDir() procura ali primeiro)
add_custom_command(TARGET BSLStage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:BSLStage>/assets
    VERBATIM)

if(MSVC)
    target_compile_options(BSLStage PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslmedia_cpu PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslsources PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_livetest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_mediatest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_gltest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_spouttest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_projecttest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_doctest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_worker PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(spoutgl PRIVATE /utf-8 /w)
    # Release sem janela de console; Debug mantem console
    target_link_options(BSLStage PRIVATE
        $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS>
        $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
    target_link_options(bslstage_worker PRIVATE /ENTRY:wWinMainCRTStartup)
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT BSLStage)
