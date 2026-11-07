#include "BslExample.h"

using namespace ffglex;

enum ParamIndex : unsigned int
{
    PARAM_BRIGHTNESS = 0,
    PARAM_SATURATION = 1
};

static CFFGLPluginInfo PluginInfo(PluginFactory<BslExample>,
                                  "BSEX",
                                  "BSL Exemplo",
                                  2, 1,
                                  1, 0,
                                  FF_EFFECT,
                                  "Brilho e saturacao (exemplo de plugin FFGL 2 para o BSL Stage)",
                                  "Dominio publico (CC0) - exemplo do BSL Stage");

static const char kVertexShader[] = R"(#version 330 core
uniform vec2 MaxUV;

layout(location = 0) in vec4 vPosition;
layout(location = 1) in vec2 vUV;

out vec2 uv;

void main()
{
    gl_Position = vPosition;
    uv = vUV * MaxUV;
}
)";

static const char kFragmentShader[] = R"(#version 330 core
uniform sampler2D InputTexture;
uniform float Brightness;
uniform float Saturation;

in vec2 uv;
out vec4 fragColor;

void main()
{
    vec4 c = texture(InputTexture, uv);

    if (c.a > 0.0)
        c.rgb /= c.a;

    c.rgb += Brightness;
    float luma = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    c.rgb = mix(vec3(luma), c.rgb, Saturation);

    c.rgb = clamp(c.rgb * c.a, vec3(0.0), vec3(c.a));
    fragColor = c;
}
)";

BslExample::BslExample()
{
    SetMinInputs(1);
    SetMaxInputs(1);

    SetParamInfof(PARAM_BRIGHTNESS, "Brilho", FF_TYPE_STANDARD);
    SetParamInfof(PARAM_SATURATION, "Saturacao", FF_TYPE_STANDARD);

    FFGLLog::LogToHost("BSL Exemplo criado");
}

BslExample::~BslExample() = default;

FFResult BslExample::InitGL(const FFGLViewportStruct* vp)
{
    if (!shader_.Compile(kVertexShader, kFragmentShader)) {
        DeInitGL();
        return FF_FAIL;
    }
    if (!quad_.Initialise()) {
        DeInitGL();
        return FF_FAIL;
    }

    return CFFGLPlugin::InitGL(vp);
}

FFResult BslExample::ProcessOpenGL(ProcessOpenGLStruct* pGL)
{
    if (pGL->numInputTextures < 1 || pGL->inputTextures[0] == nullptr)
        return FF_FAIL;

    ScopedShaderBinding shaderBinding(shader_.GetGLID());
    ScopedSamplerActivation activateSampler(0);
    Scoped2DTextureBinding textureBinding(pGL->inputTextures[0]->Handle);

    shader_.Set("InputTexture", 0);
    const FFGLTexCoords maxCoords = GetMaxGLTexCoords(*pGL->inputTextures[0]);
    shader_.Set("MaxUV", maxCoords.s, maxCoords.t);
    shader_.Set("Brightness", brightness_ * 2.0f - 1.0f);
    shader_.Set("Saturation", saturation_ * 2.0f);

    quad_.Draw();
    return FF_SUCCESS;
}

FFResult BslExample::DeInitGL()
{
    shader_.FreeGLResources();
    quad_.Release();
    return FF_SUCCESS;
}

FFResult BslExample::SetFloatParameter(unsigned int index, float value)
{
    switch (index) {
    case PARAM_BRIGHTNESS: brightness_ = value; break;
    case PARAM_SATURATION: saturation_ = value; break;
    default: return FF_FAIL;
    }
    return FF_SUCCESS;
}

float BslExample::GetFloatParameter(unsigned int index)
{
    switch (index) {
    case PARAM_BRIGHTNESS: return brightness_;
    case PARAM_SATURATION: return saturation_;
    default: return 0.0f;
    }
}
