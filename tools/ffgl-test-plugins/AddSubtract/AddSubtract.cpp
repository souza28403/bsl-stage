#include "AddSubtract.h"
using namespace ffglex;

enum ParamType : FFUInt32
{
	PT_RED,
	PT_GREEN,
	PT_BLUE
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< AddSubtract >,
	"RE01",
	"AddSub Example",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Add and Subtract colours",
	"FFGL Example"
);

static const char _vertexShaderCode[] = R"(#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
)";

static const char _fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform vec3 Brightness;

in vec2 uv;

out vec4 fragColor;

void main()
{
	vec4 color = texture( InputTexture, uv );
	if( color.a > 0.0 )
		color.rgb /= color.a;

	color.rgb += Brightness * 2. - 1.;

	color.rgb = clamp( color.rgb * color.a, vec3( 0.0 ), vec3( color.a ) );
	fragColor = color;
}
)";

AddSubtract::AddSubtract() :
	r( 0.5f ), g( 0.5f ), b( 0.5f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	SetParamInfof( PT_RED, "Brightness", FF_TYPE_RED );
	SetParamInfof( PT_GREEN, "Brightness_Green", FF_TYPE_GREEN );
	SetParamInfof( PT_BLUE, "Brightness_Blue", FF_TYPE_BLUE );

	FFGLLog::LogToHost( "Created AddSubtract effect" );
}
AddSubtract::~AddSubtract()
{
}

FFResult AddSubtract::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( _vertexShaderCode, _fragmentShaderCode ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	return CFFGLPlugin::InitGL( vp );
}
FFResult AddSubtract::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 )
		return FF_FAIL;

	if( pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( pGL->inputTextures[ 0 ]->Handle );

	shader.Set( "InputTexture", 0 );

	FFGLTexCoords maxCoords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );
	shader.Set( "MaxUV", maxCoords.s, maxCoords.t );

	glUniform3f( shader.FindUniform( "Brightness" ), r, g, b );

	quad.Draw();

	return FF_SUCCESS;
}
FFResult AddSubtract::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();

	return FF_SUCCESS;
}

FFResult AddSubtract::SetFloatParameter( unsigned int dwIndex, float value )
{
	switch( dwIndex )
	{
	case PT_RED:
		r = value;
		break;
	case PT_GREEN:
		g = value;
		break;
	case PT_BLUE:
		b = value;
		break;

	default:
		return FF_FAIL;
	}

	return FF_SUCCESS;
}

float AddSubtract::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_RED:
		return r;
	case PT_GREEN:
		return g;
	case PT_BLUE:
		return b;
	}

	return 0.0f;
}
