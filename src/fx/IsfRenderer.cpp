#include "fx/IsfRenderer.h"

#include "app/Log.h"
#include "render/TextureLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace bsl::fx {

namespace {

constexpr int kMaxPassDim = 8192;

GLuint linkProgram(GLuint vs, GLuint fs, std::string& error)
{
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        error = std::string("Falha ao ligar o shader ISF: ") + log;
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace

GLint IsfRenderer::Program::loc(const char* name)
{
    const auto it = locs.find(name);
    if (it != locs.end())
        return it->second;
    const GLint l = glGetUniformLocation(id, name);
    locs.emplace(name, l);
    return l;
}

IsfRenderer::IsfRenderer(std::shared_ptr<const IsfDoc> doc, std::filesystem::path baseDir)
    : doc_(std::move(doc)), baseDir_(std::move(baseDir))
{
    values_.resize(doc_->inputs.size());
}

IsfRenderer::~IsfRenderer()
{
    releaseGl();
}

void IsfRenderer::releaseGl()
{
    if (program_.id)
        glDeleteProgram(program_.id);
    program_ = Program{};
    if (blank_)
        glDeleteTextures(1, &blank_);
    blank_ = 0;
    buffers_.clear();
    imported_.clear();
    scratch_.reset();
}

void IsfRenderer::setParams(const media::ParamSet& params)
{
    for (size_t i = 0; i < doc_->inputs.size(); ++i) {
        const IsfInput& in = doc_->inputs[i];
        InputValue& v = values_[i];
        switch (in.type) {
        case IsfInputType::Float:
            v.f[0] = params.f(in.name);
            break;
        case IsfInputType::Bool:
            v.b = params.b(in.name);
            break;
        case IsfInputType::Long:
            v.i = static_cast<int>(std::strtoll(params.s(in.name).c_str(), nullptr, 10));
            break;
        case IsfInputType::Color: {
            const media::Color c = params.c(in.name);
            v.f[0] = c.r;
            v.f[1] = c.g;
            v.f[2] = c.b;
            v.f[3] = c.a;
            break;
        }
        case IsfInputType::Point2D: {
            const media::Json j = params.raw(in.name);
            if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number()) {
                v.f[0] = j[0].get<float>();
                v.f[1] = j[1].get<float>();
            } else {
                v.f[0] = in.defPoint[0];
                v.f[1] = in.defPoint[1];
            }
            break;
        }
        case IsfInputType::Event: {
            const int counter = params.i(in.name);
            if (haveValues_ && counter != v.eventCounter)
                v.eventPending = true;
            v.eventCounter = counter;
            break;
        }
        default:
            break;
        }
    }
    haveValues_ = true;
}

GLuint IsfRenderer::compileStage(GLenum type, const std::string& source, int preambleLines, std::string& errorOut)
{
    const GLuint sh = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (ok)
        return sh;
    char log[8192] = {};
    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
    glDeleteShader(sh);
    const char* stage = type == GL_VERTEX_SHADER ? "vertex" : "fragment";
    const IsfCompileLog mapped = remapGlslLog(log, preambleLines, stage);
    errorOut = std::string("Erro no shader ISF (") + stage + "):\n" + (mapped.text.empty() ? log : mapped.text);
    firstErrorLine_ = mapped.firstLine;
    return 0;
}

bool IsfRenderer::compile()
{
    if (compiled_)
        return error_.empty();
    const bool ok = compileImpl();
    if (!ok)
        log::warn("Shader ISF \"" + doc_->name + "\" não compilou: " + error_.substr(0, error_.find('\n', error_.find('\n') + 1)));
    return ok;
}

bool IsfRenderer::compileImpl()
{
    compiled_ = true;
    error_.clear();
    firstErrorLine_ = 0;
    warnings_ = doc_->warnings;

    sources_ = buildIsfSources(*doc_);
    GLuint vs = compileStage(GL_VERTEX_SHADER, sources_.vertex, sources_.vertexPreambleLines, error_);
    if (!vs)
        return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, sources_.fragment, sources_.fragmentPreambleLines, error_);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }
    program_.id = linkProgram(vs, fs, error_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program_.id)
        return false;

    // Imagens importadas (relativas ao shader); linha 0 = topo, entao o flip e ligado
    for (const IsfImported& im : doc_->imported) {
        Texture t = loadTexture(baseDir_ / std::filesystem::path(std::u8string(im.path.begin(), im.path.end())));
        if (!t) {
            warnings_.push_back("Imagem importada não encontrada: " + im.path);
            continue;
        }
        imported_.emplace_back(im.name, std::move(t));
    }
    return true;
}

GLuint IsfRenderer::blank()
{
    if (!blank_) {
        const unsigned char px[4] = {0, 0, 0, 0};
        glGenTextures(1, &blank_);
        glBindTexture(GL_TEXTURE_2D, blank_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return blank_;
}

IsfRenderer::Buffer* IsfRenderer::findBuffer(const std::string& name)
{
    for (Buffer& b : buffers_)
        if (b.name == name)
            return &b;
    return nullptr;
}

IsfRenderer::Buffer& IsfRenderer::ensureBuffer(const IsfPass& pass, int w, int h)
{
    Buffer* b = findBuffer(pass.target);
    if (!b) {
        buffers_.emplace_back();
        b = &buffers_.back();
        b->name = pass.target;
    }
    if (b->w != w || b->h != h || b->isFloat != pass.isFloat || !b->fbo[0]) {
        b->w = w;
        b->h = h;
        b->isFloat = pass.isFloat;
        b->persistent = pass.persistent;
        b->dual = pass.persistent;
        b->front = 0;
        b->fbo[0] = std::make_unique<Fbo>(w, h, pass.isFloat);
        b->fbo[1] = b->dual ? std::make_unique<Fbo>(w, h, pass.isFloat) : nullptr;
        for (auto& f : b->fbo) // comeca transparente
            if (f) {
                f->bind();
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
    }
    return *b;
}

Fbo& IsfRenderer::scratch(int w, int h)
{
    if (!scratch_ || scratch_->width() != w || scratch_->height() != h)
        scratch_ = std::make_unique<Fbo>(w, h);
    return *scratch_;
}

bool IsfRenderer::evalSize(const std::string& expr, int fallback, int renderW, int renderH, int& out) const
{
    out = fallback;
    if (expr.empty())
        return true;
    const IsfVariableLookup lookup = [&](const std::string& name, double& v) {
        if (name == "WIDTH") { v = renderW; return true; }
        if (name == "HEIGHT") { v = renderH; return true; }
        for (size_t i = 0; i < doc_->inputs.size(); ++i) {
            const IsfInput& in = doc_->inputs[i];
            if (in.name != name)
                continue;
            if (in.type == IsfInputType::Float) { v = values_[i].f[0]; return true; }
            if (in.type == IsfInputType::Long) { v = values_[i].i; return true; }
            if (in.type == IsfInputType::Bool) { v = values_[i].b ? 1.0 : 0.0; return true; }
        }
        return false;
    };
    double r = 0.0;
    if (!evalIsfExpression(expr, lookup, r))
        return false;
    out = std::clamp(static_cast<int>(std::lround(r)), 1, kMaxPassDim);
    return true;
}

void IsfRenderer::reset()
{
    time_ = 0.0;
    frames_ = 0;
    buffers_.clear();
}

bool IsfRenderer::render(const EffectContext& ctx, GLuint inputTex, int inW, int inH, int w, int h, Fbo& output)
{
    if (!compile())
        return false;
    if (!haveValues_)
        setParams(media::ParamSet(isfParamDescs(*doc_), media::ParamValues()));

    if (frames_ > 0)
        time_ += ctx.dt;

    // Perfil Eco: ISF pesado roda em meia resolucao e sobe com filtro bilinear
    const bool lowRes = ctx.eco && w >= 640 && h >= 360;
    const int rw = lowRes ? std::max(1, w / 2) : w;
    const int rh = lowRes ? std::max(1, h / 2) : h;

    const int passCount = doc_->passCount();
    std::vector<int> pw(static_cast<size_t>(passCount)), ph(static_cast<size_t>(passCount));
    for (int p = 0; p < passCount; ++p) {
        const IsfPass* pass = p < static_cast<int>(doc_->passes.size()) ? &doc_->passes[static_cast<size_t>(p)] : nullptr;
        int ww = rw, hh = rh;
        if (pass) {
            evalSize(pass->widthExpr, rw, rw, rh, ww);
            evalSize(pass->heightExpr, rh, rw, rh, hh);
        }
        pw[static_cast<size_t>(p)] = ww;
        ph[static_cast<size_t>(p)] = hh;
        if (pass && !pass->target.empty())
            ensureBuffer(*pass, ww, hh); // todos os buffers existem antes do primeiro passe (leituras adiantadas)
    }

    // Data
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &now);
    const float dateSeconds = static_cast<float>(lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    GLint maxUnits = 16;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits);

    glUseProgram(program_.id);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    bool lastToBuffer = false;
    Buffer* lastBuffer = nullptr;

    for (int p = 0; p < passCount; ++p) {
        const IsfPass* pass = p < static_cast<int>(doc_->passes.size()) ? &doc_->passes[static_cast<size_t>(p)] : nullptr;
        const bool isLast = p == passCount - 1;
        int ww = pw[static_cast<size_t>(p)], hh = ph[static_cast<size_t>(p)];

        Buffer* buf = (pass && !pass->target.empty()) ? findBuffer(pass->target) : nullptr;
        const Fbo* dst = nullptr;
        float flipOut = 1.0f;
        if (buf) {
            dst = buf->fbo[buf->dual ? buf->front ^ 1 : 0].get();
            if (isLast) {
                lastToBuffer = true;
                lastBuffer = buf;
            }
        } else if (isLast) {
            if (lowRes) {
                dst = &scratch(rw, rh);
            } else {
                dst = &output;
            }
            ww = dst->width();
            hh = dst->height();
            flipOut = ctx.topOrigin ? -1.0f : 1.0f;
        } else {
            dst = &scratch(ww, hh);
        }

        bindOutput(*dst, ww, hh);
        glUseProgram(program_.id);
        glUniform1i(program_.loc("PASSINDEX"), p);
        glUniform2f(program_.loc("RENDERSIZE"), static_cast<float>(ww), static_cast<float>(hh));
        glUniform1f(program_.loc("TIME"), static_cast<float>(time_));
        glUniform1f(program_.loc("TIMEDELTA"), static_cast<float>(ctx.dt));
        glUniform4f(program_.loc("DATE"), static_cast<float>(lt.tm_year + 1900), static_cast<float>(lt.tm_mon + 1),
                    static_cast<float>(lt.tm_mday), dateSeconds);
        glUniform1i(program_.loc("FRAMEINDEX"), static_cast<int>(std::min<std::uint64_t>(frames_, 0x7fffffff)));
        glUniform1f(program_.loc("_isf_flipOut"), flipOut);

        for (size_t i = 0; i < doc_->inputs.size(); ++i) {
            const IsfInput& in = doc_->inputs[i];
            const InputValue& v = values_[i];
            const GLint l = program_.loc(in.name);
            switch (in.type) {
            case IsfInputType::Float: glUniform1f(l, v.f[0]); break;
            case IsfInputType::Bool: glUniform1i(l, v.b ? 1 : 0); break;
            case IsfInputType::Long: glUniform1i(l, v.i); break;
            case IsfInputType::Color: glUniform4f(l, v.f[0], v.f[1], v.f[2], v.f[3]); break;
            case IsfInputType::Point2D: glUniform2f(l, v.f[0], v.f[1]); break;
            case IsfInputType::Event: glUniform1i(l, v.eventPending ? 1 : 0); break;
            default: break;
            }
        }

        // Samplers: uma unidade por imagem
        int unit = 0;
        for (const std::string& nm : sources_.imageNames) {
            if (unit >= maxUnits)
                break;
            GLuint tex = 0;
            float sx = 1.0f, sy = 1.0f;
            bool flip = false;
            if (nm == "inputImage" && inputTex != 0) {
                tex = inputTex;
                sx = static_cast<float>(inW);
                sy = static_cast<float>(inH);
                flip = ctx.topOrigin;
            } else if (const Buffer* b = findBuffer(nm)) {
                tex = b->fbo[b->dual ? b->front : 0]->texture();
                sx = static_cast<float>(b->w);
                sy = static_cast<float>(b->h);
            } else {
                for (const auto& [iname, itex] : imported_)
                    if (iname == nm) {
                        tex = itex.id;
                        sx = static_cast<float>(itex.width);
                        sy = static_cast<float>(itex.height);
                        flip = true;
                    }
            }
            if (!tex)
                tex = blank();
            bindTexture(unit, tex);
            glUniform1i(program_.loc(nm), unit);
            glUniform2f(program_.loc("_" + nm + "_imgSize"), sx, sy);
            glUniform1i(program_.loc("_" + nm + "_flip"), flip ? 1 : 0);
            ++unit;
        }

        vao_.draw();
        if (buf && buf->dual)
            buf->front ^= 1; // o que acabou de ser escrito passa a ser o mais recente
    }

    glActiveTexture(GL_TEXTURE0);
    for (int u = 0; u < 16; ++u) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(u));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);

    // Saida: ultimo passe num buffer (copia, com flip se a orientacao pede) ou em meia resolucao (sobe com filtro)
    if (lastToBuffer && lastBuffer) {
        const Fbo& src = *lastBuffer->fbo[lastBuffer->dual ? lastBuffer->front : 0];
        blit_.copy(src.texture(), src.width(), src.height(), output, true, ctx.topOrigin);
    } else if (lowRes) {
        blit_.copy(scratch(rw, rh).texture(), rw, rh, output, true, false);
    }

    for (InputValue& v : values_)
        v.eventPending = false;
    ++frames_;
    return true;
}

} // namespace bsl::fx
