#pragma once
#include <string>
#include <FFGLSDK.h>

class AddSubtract : public CFFGLPlugin
{
public:
	AddSubtract();
	~AddSubtract();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int dwIndex, float value ) override;

	float GetFloatParameter( unsigned int index ) override;

private:
	ffglex::FFGLShader shader;
	ffglex::FFGLScreenQuad quad;

	float r, g, b;
};
