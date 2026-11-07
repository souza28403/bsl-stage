#pragma once

#include <FFGLSDK.h>

class BslExample : public CFFGLPlugin
{
public:
    BslExample();
    ~BslExample() override;

    FFResult InitGL(const FFGLViewportStruct* vp) override;
    FFResult ProcessOpenGL(ProcessOpenGLStruct* pGL) override;
    FFResult DeInitGL() override;

    FFResult SetFloatParameter(unsigned int index, float value) override;
    float GetFloatParameter(unsigned int index) override;

private:
    ffglex::FFGLShader shader_;
    ffglex::FFGLScreenQuad quad_;

    float brightness_ = 0.5f;
    float saturation_ = 0.5f;
};
