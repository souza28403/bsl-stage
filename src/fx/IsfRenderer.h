#pragma once

#include "fx/IEffect.h"
#include "fx/FxGl.h"
#include "fx/IsfDoc.h"
#include "render/Texture.h"

#include <glad/gl.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsl::fx {

class IsfRenderer
{
public:
    IsfRenderer(std::shared_ptr<const IsfDoc> doc, std::filesystem::path baseDir);
    ~IsfRenderer();
    IsfRenderer(const IsfRenderer&) = delete;
    IsfRenderer& operator=(const IsfRenderer&) = delete;

    const IsfDoc& doc() const { return *doc_; }
    const std::shared_ptr<const IsfDoc>& docPtr() const { return doc_; }

    void setParams(const media::ParamSet& params);

    bool compile();
    bool ready() const { return compiled_ && error_.empty(); }
    bool compiledOnce() const { return compiled_; }
    const std::string& error() const { return error_; }
    int firstErrorLine() const { return firstErrorLine_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

    bool render(const EffectContext& ctx, GLuint inputTex, int inW, int inH, int w, int h, Fbo& output);

    void reset();

private:
    struct Program
    {
        GLuint id = 0;
        std::unordered_map<std::string, GLint> locs;
        GLint loc(const char* name);
        GLint loc(const std::string& name) { return loc(name.c_str()); }
    };

    struct Buffer
    {
        std::string name;
        bool persistent = false;
        bool isFloat = false;
        int w = 0, h = 0;
        std::unique_ptr<Fbo> fbo[2];
        int front = 0;
        bool dual = false;
    };

    struct InputValue
    {
        float f[4] = {0, 0, 0, 0};
        int i = 0;
        bool b = false;
        int eventCounter = 0;
        bool eventPending = false;
    };

    bool compileImpl();
    GLuint compileStage(GLenum type, const std::string& source, int preambleLines, std::string& errorOut);
    void releaseGl();
    Buffer* findBuffer(const std::string& name);
    Buffer& ensureBuffer(const IsfPass& pass, int w, int h);
    Fbo& scratch(int w, int h);
    bool evalSize(const std::string& expr, int fallback, int renderW, int renderH, int& out) const;
    GLuint blank();
    GLuint audioTexture(const EffectContext& ctx);

    std::shared_ptr<const IsfDoc> doc_;
    std::filesystem::path baseDir_;
    IsfSources sources_;
    Program program_;
    bool compiled_ = false;
    std::string error_;
    int firstErrorLine_ = 0;
    std::vector<std::string> warnings_;

    std::vector<InputValue> values_;
    bool haveValues_ = false;
    std::vector<Buffer> buffers_;
    std::vector<std::pair<std::string, Texture>> imported_;
    std::unique_ptr<Fbo> scratch_;
    GLuint blank_ = 0;
    GLuint audioTex_ = 0;
    ScreenVao vao_;
    TexBlitter blit_;

    double time_ = 0.0;
    std::uint64_t frames_ = 0;
};

}
