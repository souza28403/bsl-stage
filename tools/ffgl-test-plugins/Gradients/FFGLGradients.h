#pragma once
#include <FFGLSDK.h>

class FFGLGradients : public CFFGLPlugin
{
public:
	FFGLGradients();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int dwIndex, float value ) override;

	float GetFloatParameter( unsigned int index ) override;

private:
	struct RGBA
	{
		float red   = 1.0f;
		float green = 1.0f;
		float blue  = 0.0f;
		float alpha = 1.0f;
	};
	struct HSBA
	{
		float hue   = 0.0f;
		float sat   = 1.0f;
		float bri   = 1.0f;
		float alpha = 1.0f;
	};
	RGBA rgba1;
	HSBA hsba2;

	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;
	GLint rgbLeftLocation;
	GLint rgbRightLocation;
};
