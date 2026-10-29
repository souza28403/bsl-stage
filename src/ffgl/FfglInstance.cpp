#include <windows.h>

#include "ffgl/FfglInstance.h"

#include "app/Log.h"
#include "ffgl/FfglPlugin.h"

#include <FFGL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>

namespace bsl::ffgl {

namespace {

// ---------------------------------------------------------------------------------------------- DLLs carregadas

// Uma DLL de plugin e aberta e inicializada uma vez (FF_INITIALISE_V2); varias instancias a dividem. Quando a ultima
// instancia vai embora, FF_DEINITIALISE. A DLL nunca e descarregada (threads e servidores dos plugins).
struct Slot
{
    std::shared_ptr<Plugin> plugin;
    int users = 0;
};
std::mutex g_poolMutex;
std::map<std::string, Slot> g_pool;

std::string lowerKey(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::filesystem::path fromUtf8(const std::string& s)
{
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

void pluginLog(const std::string& message)
{
    log::info("[plugin FFGL] " + message);
}

std::shared_ptr<Plugin> acquirePlugin(const std::string& pathUtf8, std::string* error, bool* excepted)
{
    static const bool logHooked = [] {
        Plugin::setLogSink(&pluginLog);
        return true;
    }();
    (void)logHooked;

    std::lock_guard lock(g_poolMutex);
    Slot& slot = g_pool[lowerKey(pathUtf8)];
    if (!slot.plugin) {
        slot.plugin = Plugin::open(fromUtf8(pathUtf8), error);
        if (!slot.plugin) {
            g_pool.erase(lowerKey(pathUtf8));
            return nullptr;
        }
    }
    if (slot.users == 0) {
        if (!slot.plugin->initialise(error)) {
            if (excepted && error)
                *excepted = error->rfind("exceção", 0) == 0;
            return nullptr;
        }
    }
    ++slot.users;
    return slot.plugin;
}

void releasePlugin(const std::string& pathUtf8, bool skipDeinit)
{
    std::lock_guard lock(g_poolMutex);
    const auto it = g_pool.find(lowerKey(pathUtf8));
    if (it == g_pool.end() || it->second.users == 0)
        return;
    if (--it->second.users == 0 && !skipDeinit)
        it->second.plugin->deinitialise();
}

// ---------------------------------------------------------------------------------------------- estado do GL

// Guarda o estado do GL que o plugin pode mexer e o restaura no fim (o plugin recebe o estado "padrao" do FFGL)
class GlGuard
{
public:
    GlGuard()
    {
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo_);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0_);
        glActiveTexture(static_cast<GLenum>(activeTex_));
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuf_);
        blend_ = glIsEnabled(GL_BLEND);
        depth_ = glIsEnabled(GL_DEPTH_TEST);
        scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        cull_ = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcA_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstA_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEqRgb_);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEqA_);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask_);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc_);
    }

    // Estado padrao do FFGL: FBO de saida ligado e viewport do tamanho dele, blend/depth/scissor/cull desligados,
    // programa 0, VAO 0, unidade 0 ativa e sem texturas ligadas nas primeiras unidades
    static void setDefaults(GLuint fbo, int w, int h)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glDisable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ZERO);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(0);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        for (GLenum unit = 0; unit < 8; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    ~GlGuard()
    {
        while (glGetError() != GL_NO_ERROR) {
            // o plugin pode deixar erros pendentes: nao vazam para o resto do app
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vao_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuf_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex0_));
        glActiveTexture(static_cast<GLenum>(activeTex_));
        set(GL_BLEND, blend_);
        set(GL_DEPTH_TEST, depth_);
        set(GL_SCISSOR_TEST, scissor_);
        set(GL_CULL_FACE, cull_);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_), static_cast<GLenum>(blendDstRgb_),
                            static_cast<GLenum>(blendSrcA_), static_cast<GLenum>(blendDstA_));
        glBlendEquationSeparate(static_cast<GLenum>(blendEqRgb_), static_cast<GLenum>(blendEqA_));
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        glDepthMask(depthMask_);
        glDepthFunc(static_cast<GLenum>(depthFunc_));
    }
    GlGuard(const GlGuard&) = delete;
    GlGuard& operator=(const GlGuard&) = delete;

private:
    static void set(GLenum cap, GLboolean on)
    {
        if (on)
            glEnable(cap);
        else
            glDisable(cap);
    }

    GLint viewport_[4] = {};
    GLint drawFbo_ = 0, readFbo_ = 0, program_ = 0, vao_ = 0, activeTex_ = GL_TEXTURE0, tex0_ = 0, arrayBuf_ = 0;
    GLboolean blend_ = GL_FALSE, depth_ = GL_FALSE, scissor_ = GL_FALSE, cull_ = GL_FALSE;
    GLint blendSrcRgb_ = GL_ONE, blendSrcA_ = GL_ONE, blendDstRgb_ = GL_ZERO, blendDstA_ = GL_ZERO;
    GLint blendEqRgb_ = GL_FUNC_ADD, blendEqA_ = GL_FUNC_ADD;
    GLboolean colorMask_[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthMask_ = GL_TRUE;
    GLint depthFunc_ = GL_LESS;
};

// Conversao entre o alpha reto do BSL Stage e o premultiplicado do FFGL, com a inversao vertical opcional.
// texelFetch: origem e destino tem o mesmo tamanho, entao nao ha filtragem.
const char* const kConvertFragment = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uSrc;
uniform int uMode;   // 0 = reto -> premultiplicado, 1 = premultiplicado -> reto
uniform int uFlip;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (uFlip != 0)
        p.y = textureSize(uSrc, 0).y - 1 - p.y;
    vec4 c = texelFetch(uSrc, p, 0);
    if (uMode == 0) {
        c.rgb *= c.a;
    } else if (c.a > 0.0) {
        c.rgb = min(c.rgb / c.a, vec3(1.0));
    } else {
        c.rgb = vec3(0.0);
    }
    fragColor = c;
}
)";

bool isFail(Word v)
{
    return (v & 0xFFFFFFFFull) == FF_FAIL && (v >> 32) == 0;
}

void clearOutput(const Fbo& output)
{
    output.bind();
    glViewport(0, 0, output.width(), output.height());
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

} // namespace

// ----------------------------------------------------------------------------------------------- FfglInstance

FfglInstance::FfglInstance(EntryPtr entry) : entry_(std::move(entry))
{
    const size_t n = entry_->info.params.size();
    desired_.resize(n);
    sent_.resize(n);
    sentKnown_.assign(n, 0);
    eventSeen_.assign(n, std::nanf(""));
    for (size_t i = 0; i < n; ++i) {
        desired_[i].f = entry_->info.params[i].defFloat;
        desired_[i].s = entry_->info.params[i].defText;
    }
    seenEpoch_ = Catalog::instance().faultEpoch();
}

FfglInstance::~FfglInstance()
{
    teardown(!faulted_);
}

void FfglInstance::teardown(bool callPlugin)
{
    if (instance_ && plugin_ && callPlugin && wglGetCurrentContext()) { // sem contexto GL (fim do programa): so solta
        GlGuard guard;
        GlGuard::setDefaults(0, std::max(width_, 1), std::max(height_, 1));
        const CallResult r = plugin_->call(fn::DeinstantiateGL, 0, instance_);
        if (!r.ok)
            log::warn("Plugin FFGL \"" + name() + "\" falhou ao encerrar: " + exceptionText(r.exception));
    }
    instance_ = nullptr;
    width_ = height_ = 0;
    if (pluginHeld_) {
        releasePlugin(entry_->info.path, !callPlugin);
        pluginHeld_ = false;
    }
    plugin_.reset();
    std::fill(sentKnown_.begin(), sentKnown_.end(), 0);
}

void FfglInstance::fault(const std::string& what, unsigned long code)
{
    faulted_ = true;
    faultReason_ = what + (code ? " " + exceptionText(code) : std::string());
    const std::string text = "Plugin falhou: " + faultReason_;
    log::warn("Plugin FFGL \"" + name() + "\" (" + entry_->info.fileName + ") " + text);
    Catalog::instance().markSessionFault(entry_->info.path, faultReason_);
    seenEpoch_ = Catalog::instance().faultEpoch();
    // O estado interno do plugin nao e confiavel: solta a instancia sem chamar o plugin
    instance_ = nullptr;
    if (pluginHeld_) {
        releasePlugin(entry_->info.path, true);
        pluginHeld_ = false;
    }
    plugin_.reset();
}

void FfglInstance::fail(const std::string& message)
{
    if (failMessage_ != message)
        log::warn("Plugin FFGL \"" + name() + "\": " + message);
    failMessage_ = message;
}

media::SourceStatus FfglInstance::status() const
{
    if (faulted_)
        return {media::SourceStatus::Level::Error, "Plugin falhou: " + faultReason_};
    if (!failMessage_.empty())
        return {media::SourceStatus::Level::Warning, failMessage_};
    return {};
}

void FfglInstance::setParams(const media::ParamSet& params)
{
    std::vector<IndexedValue> values;
    for (size_t bi = 0; bi < entry_->map.bindings.size(); ++bi)
        toPlugin(entry_->map, bi, params, values);
    for (IndexedValue& v : values)
        if (v.index >= 0 && static_cast<size_t>(v.index) < desired_.size())
            desired_[static_cast<size_t>(v.index)] = std::move(v.value);
    haveParams_ = true;
}

bool FfglInstance::ensureReady(int w, int h)
{
    Catalog& catalog = Catalog::instance();
    // Plugin bloqueado na sessao (por esta ou outra instancia): passagem direta ate o usuario reativar
    if (catalog.faultEpoch() != seenEpoch_ || faulted_) {
        seenEpoch_ = catalog.faultEpoch();
        std::string reason;
        if (catalog.sessionBlocked(entry_->info.path, &reason)) {
            if (!faulted_) {
                faulted_ = true;
                faultReason_ = reason;
                teardown(false);
            }
            return false;
        }
        if (faulted_) { // reativado pelo usuario: recomeca do zero
            faulted_ = false;
            faultReason_.clear();
            teardown(false);
        }
        if (!instance_)
            failMessage_.clear(); // "Tentar de novo" tambem vale para quem so falhou ao iniciar
    }
    if (!failMessage_.empty() && !instance_)
        return false; // ja falhou ao iniciar: so tenta de novo quando o usuario reativar ou o catalogo mudar

    if (!pluginHeld_) {
        std::string error;
        bool excepted = false;
        plugin_ = acquirePlugin(entry_->info.path, &error, &excepted);
        if (!plugin_) {
            if (excepted)
                fault(error, 0);
            else
                fail(error.empty() ? "não foi possível carregar o plugin" : error);
            return false;
        }
        pluginHeld_ = true;
    }

    if (!instance_) {
        GlGuard guard;
        Fbo scratch(std::max(w, 1), std::max(h, 1));
        GlGuard::setDefaults(scratch.id(), w, h);
        FFGLViewportStruct vp{0, 0, static_cast<GLuint>(w), static_cast<GLuint>(h)};
        const CallResult r = plugin_->callPtr(fn::InstantiateGL, &vp);
        if (!r.ok) {
            fault("exceção em FF_INSTANTIATE_GL", r.exception);
            return false;
        }
        if (r.value == 0 || isFail(r.value)) {
            fail("o plugin recusou iniciar (FF_INSTANTIATE_GL)");
            return false;
        }
        instance_ = reinterpret_cast<void*>(r.value);
        width_ = w;
        height_ = h;
        failMessage_.clear();

        static const char kHostName[] = "BSL Stage";
        static const char kHostVersion[] = "0.4";
        SetHostinfoStruct host{kHostName, kHostVersion};
        const CallResult hi = plugin_->callPtr(fn::SetHostInfo, &host, instance_);
        if (!hi.ok) {
            fault("exceção em FF_SET_HOSTINFO", hi.exception);
            return false;
        }
        std::fill(sentKnown_.begin(), sentKnown_.end(), 0); // plugin novo: reenvia tudo
    } else if (w != width_ || h != height_) {
        GlGuard guard;
        GlGuard::setDefaults(0, w, h);
        FFGLViewportStruct vp{0, 0, static_cast<GLuint>(w), static_cast<GLuint>(h)};
        const CallResult r = plugin_->callPtr(fn::Resize, &vp, instance_);
        if (!r.ok) {
            fault("exceção em FF_RESIZE", r.exception);
            return false;
        }
        width_ = w;
        height_ = h;
    }
    return true;
}

void FfglInstance::flushParams()
{
    if (!instance_ || !plugin_)
        return;
    auto send = [&](size_t i, const PluginValue& v, bool text) -> bool {
        SetParameterStruct sp{};
        sp.ParameterNumber = static_cast<FFUInt32>(i);
        if (text)
            sp.NewParameterValue.PointerValue = const_cast<char*>(v.s.c_str());
        else
            sp.NewParameterValue.UIntValue = static_cast<FFUInt32>(floatWord(v.f));
        const CallResult r = plugin_->callPtr(fn::SetParameter, &sp, instance_);
        if (!r.ok) {
            fault("exceção em FF_SET_PARAMETER (" + entry_->info.params[i].name + ")", r.exception);
            return false;
        }
        return true;
    };

    for (const int i : pulseOff_) { // eventos disparados no quadro anterior voltam a zero
        PluginValue off;
        off.f = 0.0f;
        if (!send(static_cast<size_t>(i), off, false))
            return;
    }
    pulseOff_.clear();

    for (size_t i = 0; i < desired_.size(); ++i) {
        const ParamInfo& pi = entry_->info.params[i];
        if (pi.type == kPBuffer)
            continue;
        if (pi.type == kPEvent) {
            const float counter = desired_[i].f;
            if (std::isnan(eventSeen_[i])) {
                eventSeen_[i] = counter;
            } else if (counter != eventSeen_[i]) {
                eventSeen_[i] = counter;
                PluginValue on;
                on.f = 1.0f;
                if (!send(i, on, false))
                    return;
                pulseOff_.push_back(static_cast<int>(i));
            }
            continue;
        }
        const bool text = pi.type == kPText || pi.type == kPFile;
        if (sentKnown_[i] && !pluginValueDiffers(desired_[i], sent_[i]))
            continue;
        if (!send(i, desired_[i], text))
            return;
        sent_[i] = desired_[i];
        sentKnown_[i] = 1;
    }
}

void FfglInstance::convert(GLuint src, int w, int h, const Fbo& dst, bool unpremultiply, bool flip)
{
    if (!convert_)
        convert_ = std::make_unique<Shader>(fx::kFxVertexSource, kConvertFragment);
    fx::bindOutput(dst, w, h);
    convert_->use();
    convert_->setInt("uSrc", 0);
    convert_->setInt("uMode", unpremultiply ? 1 : 0);
    convert_->setInt("uFlip", flip ? 1 : 0);
    fx::bindTexture(0, src);
    vao_.draw();
    fx::bindTexture(0, 0);
    glUseProgram(0);
}

void FfglInstance::pass(GLuint inputTex, int w, int h, Fbo& output)
{
    if (needsInput() && inputTex != 0)
        blit_.copy(inputTex, w, h, output);
    else
        clearOutput(output);
}

void FfglInstance::process(const fx::EffectContext& ctx, GLuint inputTex, int w, int h, Fbo& output)
{
    const bool wantsInput = needsInput();
    if (w <= 0 || h <= 0 || (wantsInput && inputTex == 0) || entry_->info.type == kTypeMixer) {
        pass(inputTex, w, h, output);
        return;
    }
    if (!ensureReady(w, h)) {
        pass(inputTex, w, h, output);
        return;
    }

    FboPool* pool = ctx.pool;
    if (!pool) {
        if (!ownPool_)
            ownPool_ = std::make_unique<FboPool>();
        pool = ownPool_.get();
    }

    bool ok = false;
    FboPool::Lease mid = pool->acquire(w, h);
    {
        GlGuard guard;
        // Entrada: do alpha reto para o premultiplicado do FFGL (e linha 0 na base), escrita no proprio destino, que so
        // volta a ser escrito depois que o plugin termina de ler
        if (wantsInput)
            convert(inputTex, w, h, output, false, ctx.topOrigin);

        flushParams();
        if (!faulted_ && instance_) {
            SetBeatinfoStruct beat{};
            beat.bpm = ctx.bpm;
            beat.barPhase = ctx.barPhase;
            lastBpm_ = ctx.bpm;
            lastBarPhase_ = ctx.barPhase;
            double seconds = ctx.time;
            CallResult r = plugin_->callPtr(fn::SetBeatInfo, &beat, instance_);
            if (!r.ok) {
                fault("exceção em FF_SET_BEATINFO", r.exception);
            } else {
                r = plugin_->callPtr(fn::SetTime, &seconds, instance_);
                if (!r.ok)
                    fault("exceção em FF_SET_TIME", r.exception);
            }
        }
        if (!faulted_ && instance_) {
            GlGuard::setDefaults(mid->id(), w, h);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            FFGLTextureStruct tex{};
            tex.Width = tex.HardwareWidth = static_cast<FFUInt32>(w);
            tex.Height = tex.HardwareHeight = static_cast<FFUInt32>(h);
            tex.Handle = wantsInput ? output.texture() : 0;
            FFGLTextureStruct* inputs[1] = {&tex};
            ProcessOpenGLStruct pogl{};
            pogl.numInputTextures = wantsInput ? 1 : 0;
            pogl.inputTextures = inputs;
            pogl.HostFBO = mid->id();
            const CallResult r = plugin_->callPtr(fn::ProcessOpenGL, &pogl, instance_);
            if (!r.ok) {
                fault("exceção em FF_PROCESS_OPENGL", r.exception);
            } else if (r.u32() == FF_FAIL) { // so os 32 bits baixos valem (resultado FFResult)
                fail("o plugin recusou o quadro (FF_PROCESS_OPENGL)");
            } else {
                failMessage_.clear();
                ok = true;
                ++frames_;
            }
        }
    }

    if (ok)
        convert(mid->texture(), w, h, output, true, ctx.topOrigin); // premultiplicado -> reto, linha 0 de volta
    else
        pass(inputTex, w, h, output);
}

bool FfglInstance::pollParamChanges(media::ParamValues& values)
{
    using namespace std::chrono;
    if (!instance_ || !plugin_ || faulted_)
        return false;
    const auto now = steady_clock::now();
    if (now - lastPoll_ < milliseconds(100))
        return false;
    lastPoll_ = now;

    // Le o que o plugin tem agora (um plugin com interface propria pode ter mudado por conta propria)
    std::vector<PluginValue> current = sent_;
    std::vector<char> changed(sent_.size(), 0);
    for (size_t i = 0; i < sent_.size(); ++i) {
        const ParamInfo& pi = entry_->info.params[i];
        if (pi.type == kPBuffer || pi.type == kPEvent || !sentKnown_[i])
            continue;
        const CallResult r = plugin_->call(fn::GetParameter, static_cast<Word>(i), instance_);
        if (!r.ok) {
            fault("exceção em FF_GET_PARAMETER", r.exception);
            return false;
        }
        if (pi.type == kPText || pi.type == kPFile) {
            if (r.value == 0 || isFail(r.value))
                continue;
            current[i].s = plugin_->readString(r.value, 4096);
        } else {
            if (r.u32() == FF_FAIL)
                continue;
            const float f = r.f32();
            if (!std::isfinite(f))
                continue;
            current[i].f = f;
        }
        changed[i] = pluginValueDiffers(current[i], sent_[i]) ? 1 : 0;
    }

    bool any = false;
    for (size_t bi = 0; bi < entry_->map.bindings.size(); ++bi) {
        const Binding& b = entry_->map.bindings[bi];
        if (b.kind == BindKind::Event)
            continue;
        bool diff = false;
        for (int k = 0; k < b.count; ++k)
            diff = diff || (b.params[k] >= 0 && changed[static_cast<size_t>(b.params[k])]);
        if (!diff)
            continue;
        values.set(entry_->map.descs[bi].id, fromPlugin(entry_->map, bi, current));
        for (int k = 0; k < b.count; ++k) {
            const size_t idx = static_cast<size_t>(b.params[k]);
            desired_[idx] = sent_[idx] = current[idx];
        }
        any = true;
    }
    return any;
}

} // namespace bsl::ffgl
