#include <FFGLSDK.h>

using namespace ffglex;

#if defined(PROBE_SOURCE)
#define PROBE_ID "TPSR"
#define PROBE_NAME "Test probe src"
#define PROBE_TYPE FF_SOURCE
#define PROBE_INPUTS 0
#else
#define PROBE_ID "TPEF"
#define PROBE_NAME "Test probe fx"
#define PROBE_TYPE FF_EFFECT
#define PROBE_INPUTS 1
#endif

class Probe : public CFFGLPlugin
{
public:
    Probe() : level( 0.5f ), bpm( 0.0f ), barPhase( 0.0f )
    {
        SetMinInputs( PROBE_INPUTS );
        SetMaxInputs( PROBE_INPUTS );
        SetParamInfof( 0, "Level", FF_TYPE_STANDARD );
        SetParamInfof( 1, "BPM", FF_TYPE_STANDARD );
        SetParamInfof( 2, "BarPhase", FF_TYPE_STANDARD );
    }

    FFResult InitGL( const FFGLViewportStruct* vp ) override;
    FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
    FFResult DeInitGL() override;
    FFResult SetFloatParameter( unsigned int index, float value ) override
    {
        if( index == 0 )
            level = value;
        return index < 3 ? FF_SUCCESS : FF_FAIL;
    }
    float GetFloatParameter( unsigned int index ) override
    {
        return index == 0 ? level : index == 1 ? bpm / 1000.0f : index == 2 ? barPhase : 0.0f;
    }
    void SetBeatInfo( float b, float phase ) override
    {
        bpm      = b;
        barPhase = phase;
    }

private:
    ffglex::FFGLShader shader;
    ffglex::FFGLScreenQuad quad;
    float level, bpm, barPhase;
};

static CFFGLPluginInfo PluginInfo(
    PluginFactory< Probe >, PROBE_ID, PROBE_NAME, 2, 1, 1, 0, PROBE_TYPE, "BSL Stage test probe", "BSL Stage (test)" );

static const char vertexShaderCode[] = R"(#version 410 core
uniform vec2 MaxUV;
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
out vec2 screenUv;
void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
	screenUv = vUV;
}
)";

#if defined(PROBE_SOURCE)
static const char fragmentShaderCode[] = R"(#version 410 core
uniform float Level;
in vec2 uv;
in vec2 screenUv;
out vec4 fragColor;
void main()
{
	fragColor = vec4( screenUv.y, 0.0, Level, 1.0 );
}
)";
#else
static const char fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform float Level;
in vec2 uv;
in vec2 screenUv;
out vec4 fragColor;
void main()
{
	fragColor = screenUv.y < 0.5 ? texture( InputTexture, uv ) : vec4( 0.0, 0.0, Level, 1.0 );
}
)";
#endif

FFResult Probe::InitGL( const FFGLViewportStruct* vp )
{
    if( !shader.Compile( vertexShaderCode, fragmentShaderCode ) || !quad.Initialise() )
    {
        DeInitGL();
        return FF_FAIL;
    }
    return CFFGLPlugin::InitGL( vp );
}

FFResult Probe::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
    ScopedShaderBinding shaderBinding( shader.GetGLID() );
    shader.Set( "Level", level );
#if !defined(PROBE_SOURCE)
    if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == NULL )
        return FF_FAIL;
    ScopedSamplerActivation activateSampler( 0 );
    Scoped2DTextureBinding textureBinding( pGL->inputTextures[ 0 ]->Handle );
    shader.Set( "InputTexture", 0 );
    FFGLTexCoords maxCoords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );
    shader.Set( "MaxUV", maxCoords.s, maxCoords.t );
#else
    shader.Set( "MaxUV", 1.0f, 1.0f );
    (void)pGL;
#endif
    quad.Draw();
    return FF_SUCCESS;
}

FFResult Probe::DeInitGL()
{
    shader.FreeGLResources();
    quad.Release();
    return FF_SUCCESS;
}
