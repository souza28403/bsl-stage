cmake_minimum_required(VERSION 3.25)

set(_bsl_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_bsl_standalone ON)
endif()
if(_bsl_standalone)
    project(BSLFfglTemplate LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(BSL_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "Raiz do repositorio do BSL Stage")
set(FFGL_SDK_DIR "${BSL_REPO_DIR}/tools/ffgl-test-plugins" CACHE PATH "Pasta com sdk/ e shim/ do FFGL SDK 2.x")

if(NOT EXISTS "${FFGL_SDK_DIR}/sdk/ffgl/FFGLPluginSDK.h")
    message(FATAL_ERROR "FFGL SDK nao encontrado em ${FFGL_SDK_DIR}/sdk. Veja sdk/ffgl-template/README.md.")
endif()

if(NOT TARGET glad)
    add_library(glad STATIC "${BSL_REPO_DIR}/third_party/glad/src/gl.c")
    target_include_directories(glad PUBLIC "${BSL_REPO_DIR}/third_party/glad/include")
endif()

file(GLOB BSL_FFGL_SDK_SOURCES "${FFGL_SDK_DIR}/sdk/ffgl/*.cpp" "${FFGL_SDK_DIR}/sdk/ffglex/*.cpp")

add_library(bsl_ffgl_sdk OBJECT ${BSL_FFGL_SDK_SOURCES} "${FFGL_SDK_DIR}/shim/glew_shim.c")
target_include_directories(bsl_ffgl_sdk PRIVATE "${FFGL_SDK_DIR}/shim" "${FFGL_SDK_DIR}/sdk")
target_compile_definitions(bsl_ffgl_sdk PRIVATE NOMINMAX)
target_link_libraries(bsl_ffgl_sdk PRIVATE glad)

add_library(bsl_ffgl_example SHARED src/BslExample.cpp $<TARGET_OBJECTS:bsl_ffgl_sdk>)
set_target_properties(bsl_ffgl_example PROPERTIES OUTPUT_NAME "BSLExample")
target_include_directories(bsl_ffgl_example PRIVATE src)
target_include_directories(bsl_ffgl_example SYSTEM PRIVATE "${FFGL_SDK_DIR}/shim" "${FFGL_SDK_DIR}/sdk")
target_compile_definitions(bsl_ffgl_example PRIVATE NOMINMAX)
target_link_libraries(bsl_ffgl_example PRIVATE glad opengl32)

if(MSVC)
    target_compile_options(bsl_ffgl_sdk PRIVATE /W0 /utf-8)
    target_compile_options(bsl_ffgl_example PRIVATE /W4 /utf-8)
endif()
