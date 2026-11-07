cmake_minimum_required(VERSION 3.25)
project(BSLStage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

set(BSLSTAGE_GITHUB_REPO "bsl-light/bsl-stage" CACHE STRING "Repositorio do GitHub (dono/nome) usado nas atualizacoes e nos reportes")
configure_file(src/app/Version.h.in "${CMAKE_BINARY_DIR}/generated/app/Version.h" @ONLY)

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

FetchContent_MakeAvailable(glfw imgui nlohmann_json stb spout2)

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

set(BSL_ORT_VERSION "1.24.4")
set(BSL_ORT_SHA256 "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681")
set(BSL_DML_VERSION "1.15.4")
set(BSL_DML_SHA256 "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9")
set(BSL_ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/ort-${BSL_ORT_VERSION}")
set(BSL_DML_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/directml-${BSL_DML_VERSION}")
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
if(NOT EXISTS "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll")
    bsl_fetch_nupkg("microsoft.ml.onnxruntime.directml" "${BSL_ORT_VERSION}" "${BSL_ORT_SHA256}" "${BSL_ORT_ROOT}")
endif()
if(NOT EXISTS "${BSL_DML_ROOT}/bin/x64-win/DirectML.dll")
    bsl_fetch_nupkg("microsoft.ai.directml" "${BSL_DML_VERSION}" "${BSL_DML_SHA256}" "${BSL_DML_ROOT}")
endif()
set(BSL_ORT_DLLS
    "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll"
    "${BSL_ORT_ROOT}/runtimes/win-x64/native/onnxruntime_providers_shared.dll"
    "${BSL_DML_ROOT}/bin/x64-win/DirectML.dll")

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
set(_hf_depth "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/4472b7362082ad9968fee890ca0f1e5aca36b93d/onnx")
set(_hf_modnet "https://huggingface.co/Xenova/modnet/resolve/fa2fa546052fba4c08921230a26cc69a333fca12/onnx")
bsl_fetch_model("depth_anything_v2_small_fp16.onnx" "${_hf_depth}/model_fp16.onnx"
    "2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04")
bsl_fetch_model("modnet_fp32.onnx" "${_hf_modnet}/model.onnx"
    "07c308cf0fc7e6e8b2065a12ed7fc07e1de8febb7dc7839d7b7f15dd66584df9")

add_library(stb_image STATIC third_party/stb_image_impl.c third_party/stb_image_write_impl.c
    third_party/stb_truetype_impl.c)
target_include_directories(stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})

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

add_library(bslffgl_core STATIC
    src/ffgl/FfglInfo.cpp
    src/ffgl/FfglPe.cpp
    src/ffgl/FfglCall.cpp
    src/ffgl/FfglPlugin.cpp
    src/ffgl/FfglParams.cpp)
target_include_directories(bslffgl_core PUBLIC src)
target_include_directories(bslffgl_core SYSTEM PUBLIC third_party/ffgl)
target_compile_definitions(bslffgl_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslffgl_core PUBLIC glad nlohmann_json::nlohmann_json)

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
target_link_libraries(bslupdate_core PUBLIC nlohmann_json::nlohmann_json winhttp shell32 ole32)

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
    src/media/ControlPanel.cpp
    src/control/Keys.cpp
    src/control/ActionRegistry.cpp
    src/control/KeyBinding.cpp
    src/control/KeyDispatcher.cpp
    src/control/KeyService.cpp
    src/control/SystemHotkeys.cpp
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
    src/media/SpoutInput.cpp
    src/media/SpoutSource.cpp
    src/render/Shader.cpp
    src/render/Fbo.cpp
    src/render/FboPool.cpp
    src/render/TextureLoader.cpp
    src/render/TestPatternSource.cpp
    src/render/SolidColorSource.cpp
    src/render/D3D11Interop.cpp
    src/render/ZoneMasks.cpp
    src/ai/AiRuntime.cpp
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
    src/ffgl/FfglCatalog.cpp
    src/ffgl/FfglInstance.cpp
    src/ffgl/FfglSource.cpp
    src/ffgl/FfglProvider.cpp
    src/ffgl/FfglImport.cpp
    src/plugin/PluginTemplates.cpp
    src/plugin/PluginFiles.cpp
    src/plugin/PluginPackage.cpp
    src/plugin/IsfCheck.cpp
    src/audio/AudioDsp.cpp
    src/audio/AudioAnalyzer.cpp
    src/fx/bsl/BslCommon.cpp
    src/fx/bsl/BslProvider.cpp
    src/fx/bsl/Vhs.cpp
    src/fx/bsl/Relight25D.cpp
    src/fx/bsl/BodyGlitch.cpp
    src/fx/bsl/PerformerAura.cpp
    src/fx/bsl/ZSlice.cpp
    src/fx/bsl/SliceRefiner2.cpp
    src/fx/bsl/SliceFeed.cpp
    src/output/PixelMap.cpp
    src/output/PixelMapMapping.cpp
    src/output/PatternRaster.cpp
    src/fx/bsl/PixelMapSource.cpp
    src/net/MiniHttp.cpp
    src/fx/bsl/SliceState.cpp
    src/fx/bsl/BslPreview.cpp
    src/fx/bsl/SliceRefiner3.cpp
    src/fx/bsl/WebStudio.cpp
    src/fx/bsl/BslWebSources.cpp)
target_include_directories(bslsources PUBLIC src)
target_include_directories(bslsources SYSTEM PRIVATE "${BSL_ORT_ROOT}/build/native/include" "${BSL_DML_ROOT}/include")
target_include_directories(bslsources PUBLIC third_party/ndi-min)
target_compile_definitions(bslsources PUBLIC GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslsources PUBLIC glad bslffgl_core bslmedia_cpu bslupdate_core nlohmann_json::nlohmann_json
    spoutgl opengl32 d3d11 d3d12 dxgi dwmapi windowsapp runtimeobject shell32 ole32 uuid ws2_32 iphlpapi)

add_executable(BSLStage
    src/main.cpp
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
    src/io/FileDialogs.cpp
    src/io/ProjectFile.cpp
    src/output/OutputSetup.cpp
    src/output/Warp.cpp
    src/output/Snap.cpp
    src/output/MapEdit.cpp
    src/output/MapHistory.cpp
    src/output/SpoutOutput.cpp
    src/output/NdiOutput.cpp
    src/output/TextLabel.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp
    src/render/Compositor.cpp
    src/render/LiveThumb.cpp
    src/render/ThumbnailCache.cpp
    src/ui/Brand.cpp
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
    src/ui/panels/ParamWidgets.cpp
    src/ui/panels/SliceEditor.cpp
    src/ui/panels/CaptureTargetChooser.cpp
    src/ui/panels/VideoCompat.cpp
    src/ui/panels/UpdateUi.cpp
    src/ui/panels/ReportWindow.cpp
    src/ui/panels/LegalWindow.cpp)

target_include_directories(BSLStage PRIVATE src)
target_compile_definitions(BSLStage PRIVATE GLFW_INCLUDE_NONE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(BSLStage PRIVATE bslsources imgui glfw nlohmann_json::nlohmann_json bslmedia_cpu spoutgl shell32 ole32 uuid winmm psapi)
add_dependencies(BSLStage bslstage_worker bslstage_updater)

add_executable(bslstage_updater WIN32 src/update/updater_main.cpp)
target_link_libraries(bslstage_updater PRIVATE bslupdate_core comctl32)
target_link_options(bslstage_updater PRIVATE /ENTRY:wWinMainCRTStartup)

set(BSL_CEF_DIR "C:/Users/GABRIEL - BSL LIGHT/Desktop/ffgl-master/deps/cef" CACHE PATH
    "Distribuicao binaria do CEF para a fonte Web. Vazio ou inexistente = Web desabilitada")
set(BSL_HAVE_CEF OFF)
if(BSL_CEF_DIR AND EXISTS "${BSL_CEF_DIR}/include/cef_version.h" AND EXISTS "${BSL_CEF_DIR}/Release/libcef.lib")
    set(BSL_HAVE_CEF ON)
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
else()
    message(WARNING "CEF nao encontrado (BSL_CEF_DIR=\"${BSL_CEF_DIR}\"): a fonte Web fica desabilitada.")
endif()

set(BSL_WORKER_SOURCES
    src/worker/WorkerMain.cpp
    src/worker/WorkerUtil.cpp
    src/worker/PipeChannel.cpp
    src/worker/FrameSink.cpp
    src/worker/PdfDocument.cpp
    src/worker/OfficeConverter.cpp
    src/worker/FfglScan.cpp)
add_executable(bslstage_worker WIN32 ${BSL_WORKER_SOURCES})
target_include_directories(bslstage_worker PRIVATE src)
target_include_directories(bslstage_worker SYSTEM PRIVATE "${BSL_PDFIUM_ROOT}/include")
target_compile_definitions(bslstage_worker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(bslstage_worker PRIVATE bslffgl_core nlohmann_json::nlohmann_json "${BSL_PDFIUM_ROOT}/lib/pdfium.dll.lib"
    d3d11 dxgi d3dcompiler ole32 oleaut32 uuid shell32 advapi32 user32)
add_custom_command(TARGET bslstage_worker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BSL_PDFIUM_ROOT}/bin/pdfium.dll" $<TARGET_FILE_DIR:bslstage_worker>
    VERBATIM)

if(BSL_HAVE_CEF)
    target_sources(bslstage_worker PRIVATE src/worker/WebSession.cpp src/worker/PanelWindow.cpp)
    target_compile_definitions(bslstage_worker PRIVATE BSL_WITH_CEF)
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

add_executable(bslstage_mediatest tools/mediatest.cpp)
target_link_libraries(bslstage_mediatest PRIVATE bslmedia_cpu)

add_executable(bslstage_gltest tools/gltest.cpp)
target_link_libraries(bslstage_gltest PRIVATE bslsources glfw bslmedia_cpu)

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

add_executable(bslstage_bsltest tools/bsltest.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp src/output/NdiOutput.cpp
    src/output/SpoutOutput.cpp src/render/Compositor.cpp)
target_include_directories(bslstage_bsltest PRIVATE src)
target_link_libraries(bslstage_bsltest PRIVATE bslsources glfw bslmedia_cpu spoutgl)
add_dependencies(bslstage_bsltest bslstage_worker)

add_executable(bslstage_outputstress tools/outputstress.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp src/output/NdiOutput.cpp
    src/output/SpoutOutput.cpp src/render/Compositor.cpp)
target_include_directories(bslstage_outputstress PRIVATE src)
target_link_libraries(bslstage_outputstress PRIVATE bslsources glfw bslmedia_cpu spoutgl psapi)
add_dependencies(bslstage_outputstress bslstage_worker)

add_executable(bslstage_transformtest tools/transformtest.cpp src/output/OutputSetup.cpp src/core/Clip.cpp src/core/Composition.cpp
    src/core/TransformHistory.cpp src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp src/output/Warp.cpp src/output/TextLabel.cpp
    src/output/NdiOutput.cpp src/output/SpoutOutput.cpp src/render/Compositor.cpp)
target_include_directories(bslstage_transformtest PRIVATE src)
target_link_libraries(bslstage_transformtest PRIVATE bslsources glfw bslmedia_cpu spoutgl)
add_dependencies(bslstage_transformtest bslstage_worker)

add_executable(bslstage_aitest tools/aitest.cpp)
target_include_directories(bslstage_aitest PRIVATE src)
target_link_libraries(bslstage_aitest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_doctest tools/doctest.cpp src/worker/OfficeConverter.cpp src/worker/WorkerUtil.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_doctest PRIVATE src)
target_link_libraries(bslstage_doctest PRIVATE bslsources glfw bslmedia_cpu ole32 oleaut32 uuid)
add_dependencies(bslstage_doctest bslstage_worker)

add_executable(bslstage_spouttest tools/spouttest.cpp tools/spouttest_source.cpp src/output/SpoutOutput.cpp)
target_include_directories(bslstage_spouttest PRIVATE src)
target_compile_definitions(bslstage_spouttest PRIVATE NOMINMAX)
target_link_libraries(bslstage_spouttest PRIVATE bslsources glfw bslmedia_cpu spoutgl)

add_executable(bslstage_ndittest tools/ndittest.cpp
    src/output/NdiOutput.cpp
    src/output/OutputRenderer.cpp src/output/OutputPresenter.cpp
    src/output/OutputSetup.cpp
    src/output/Warp.cpp
    src/output/TextLabel.cpp
    src/output/SpoutOutput.cpp
    src/render/Compositor.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_ndittest PRIVATE src)
target_link_libraries(bslstage_ndittest PRIVATE bslsources glfw bslmedia_cpu spoutgl)

add_executable(bslstage_projecttest tools/projecttest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp
    src/output/OutputSetup.cpp)
target_link_libraries(bslstage_projecttest PRIVATE bslsources nlohmann_json::nlohmann_json bslmedia_cpu shell32 ole32)

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
target_link_libraries(bslstage_pixelmaptest PRIVATE bslsources glfw bslmedia_cpu)

add_executable(bslstage_dmxtest tools/dmxtest.cpp
    src/io/ProjectFile.cpp
    src/core/Clip.cpp src/core/Composition.cpp src/output/OutputSetup.cpp src/ui/ViewState.cpp)
target_include_directories(bslstage_dmxtest PRIVATE src)
target_link_libraries(bslstage_dmxtest PRIVATE bslsources glfw bslmedia_cpu nlohmann_json::nlohmann_json shell32 ole32)

add_executable(bslstage_uitest tools/uitest.cpp src/ui/LayoutPreset.cpp src/ui/ViewState.cpp
    src/legal/Notices.cpp src/app/Settings.cpp src/app/PerformanceProfile.cpp
    src/core/Clip.cpp src/core/Composition.cpp)
target_include_directories(bslstage_uitest PRIVATE src)
target_compile_definitions(bslstage_uitest PRIVATE BSL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(bslstage_uitest PRIVATE nlohmann_json::nlohmann_json bslsources glfw bslmedia_cpu)

add_executable(bslstage_updatetest tools/updatetest.cpp)
target_link_libraries(bslstage_updatetest PRIVATE bslupdate_core)
add_dependencies(bslstage_updatetest bslstage_updater)
target_compile_definitions(bslstage_updatetest PRIVATE BSL_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tools/fixtures")

foreach(_tgt BSLStage bslstage_spouttest bslstage_ndittest bslstage_pixelmaptest bslstage_mediatest bslstage_gltest bslstage_projecttest bslstage_livetest bslstage_doctest bslstage_fxtest bslstage_ffgltest bslstage_bsltest bslstage_outputstress bslstage_aitest bslstage_keystest bslstage_transformtest bslstage_dmxtest bslstage_isfcheck bslstage_plugintest)
    add_custom_command(TARGET ${_tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BSL_FFMPEG_DLLS} ${BSL_ORT_DLLS} $<TARGET_FILE_DIR:${_tgt}>
        VERBATIM)
endforeach()

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
    target_compile_options(BSLStage PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslmedia_cpu PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslffgl_core PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslsources PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_livetest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_mediatest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_gltest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_spouttest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_ndittest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_projecttest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_maptest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_uitest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_pixelmaptest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_doctest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_fxtest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_ffgltest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_bsltest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_outputstress PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_aitest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_keystest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_transformtest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_dmxtest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_isfcheck PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_plugintest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslupdate_core PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_updater PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_updatetest PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(bslstage_worker PRIVATE /W4 /utf-8 /permissive-)
    target_compile_options(spoutgl PRIVATE /utf-8 /w)
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
