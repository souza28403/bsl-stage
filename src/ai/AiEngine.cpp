#include "ai/AiEngine.h"

#include "app/Log.h"
#include "fx/FxGl.h"
#include "render/Fbo.h"
#include "render/Shader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace bsl::ai {

namespace {

using Clock = std::chrono::steady_clock;

// Reducao da entrada para o tamanho do modelo: 16 amostras bilineares por pixel (caixa 8x8 efetiva), na orientacao
// "linha 0 = topo da imagem", seja qual for a da textura de entrada. Alpha vira preto (sobre fundo preto).
const char* const kCaptureFrag = R"(#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uTex;
uniform int uTop;
uniform vec2 uStep; // uma amostra de saida, em UV da entrada
void main()
{
    vec3 sum = vec3(0.0);
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) {
            vec2 off = (vec2(float(i), float(j)) + 0.5) / 4.0 - 0.5;
            vec2 p = vUv + off * uStep;
            vec2 q = uTop == 1 ? p : vec2(p.x, 1.0 - p.y);
            vec4 c = texture(uTex, q);
            sum += c.rgb * c.a;
        }
    fragColor = vec4(sum / 16.0, 1.0);
}
)";

constexpr float kDepthSmooth = 0.55f;  // peso do resultado novo na suavizacao temporal da profundidade
constexpr float kPersonSmooth = 0.65f; // idem, mascara
constexpr float kRangeSmooth = 0.25f;  // faixa (minimo/maximo) da normalizacao da profundidade
constexpr double kIdleSeconds = 4.0;   // fonte sem pedidos por tanto tempo e liberada

// Resultado de uma (fonte, modelo), compartilhado entre a thread de render e a da IA (sob o mutex do motor)
struct Slot
{
    std::uint64_t submitted = 0; // jobs enviados
    std::uint64_t finished = 0;  // jobs concluidos (com ou sem sucesso)
    std::uint64_t version = 0;   // muda a cada resultado novo
    int w = 0, h = 0;
    std::vector<float> data;          // profundidade 0..1 ou alfa 0..1, suavizado no tempo
    std::vector<std::uint8_t> bytes;  // so da pessoa (R8)
    float lo = 0.0f, hi = 1.0f;       // faixa da normalizacao
    bool haveRange = false;
};

struct Job
{
    std::uint64_t source = 0;
    int kind = 0;
    std::vector<std::uint8_t> rgba;
};

// Lado GL de uma (fonte, modelo)
struct Capture
{
    std::unique_ptr<Fbo> fbo;
    GLuint pbo = 0;
    GLsync fence = nullptr;
    bool pending = false;
    Clock::time_point last{};
    bool haveLast = false;
};

struct Entry
{
    std::array<Capture, kModelKindCount> cap;
    std::array<GLuint, kModelKindCount> tex{};
    std::array<int, kModelKindCount> texW{}, texH{};
    std::array<std::uint64_t, kModelKindCount> uploaded{};
    Clock::time_point lastUsed{};
};

void destroyEntry(Entry& e)
{
    for (int k = 0; k < kModelKindCount; ++k) {
        Capture& c = e.cap[static_cast<size_t>(k)];
        if (c.fence)
            glDeleteSync(c.fence);
        c.fence = nullptr;
        if (c.pbo)
            glDeleteBuffers(1, &c.pbo);
        c.pbo = 0;
        c.fbo.reset();
        GLuint& t = e.tex[static_cast<size_t>(k)];
        if (t)
            glDeleteTextures(1, &t);
        t = 0;
    }
}

// Cena sintetica para o "Testar": degrade de fundo com listras e uma caixa clara em primeiro plano (linha 0 = topo)
std::vector<std::uint8_t> makeTestScene(int w, int h)
{
    std::vector<std::uint8_t> rgb(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool box = x > w * 3 / 10 && x < w * 7 / 10 && y > h * 3 / 10 && y < h * 9 / 10;
            std::uint8_t* p = &rgb[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3];
            if (box) {
                p[0] = static_cast<std::uint8_t>(200 + 40 * y / h);
                p[1] = static_cast<std::uint8_t>(120 + 60 * x / w);
                p[2] = 90;
            } else {
                const int stripe = ((x / 12) + (y / 12)) & 1;
                p[0] = static_cast<std::uint8_t>(40 + 30 * y / h + stripe * 14);
                p[1] = static_cast<std::uint8_t>(60 + 50 * y / h + stripe * 14);
                p[2] = static_cast<std::uint8_t>(110 + 60 * y / h);
            }
        }
    return rgb;
}

} // namespace

struct AiEngine::Impl
{
    // --- Estado compartilhado (mutex) ---
    mutable std::mutex m;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::unordered_map<std::uint64_t, std::array<Slot, kModelKindCount>> slots;
    bool stop = false;
    bool testRequested = false;
    int profileRate = 15;
    int rateOverride = 0;
    bool profileOn = true;
    DevicePref pref = DevicePref::Auto;
    std::uint64_t prefGeneration = 0;
    ModelStats model[kModelKindCount];
    std::deque<Clock::time_point> doneTimes;
    int activeSources = 0;
    bool testRunning = false, testDone = false;
    std::string testReport;
    Clock::time_point presentChecked{};
    bool presentCache[kModelKindCount] = {false, false};
    std::map<std::uint64_t, std::pair<GLuint, GLuint>> overrides;
    Clock::time_point failedAt[kModelKindCount]{};
    bool retryFlag[kModelKindCount] = {false, false};

    // --- So a thread de render ---
    std::unordered_map<std::uint64_t, Entry> entries;
    std::unique_ptr<Shader> capShader;
    std::unique_ptr<fx::ScreenVao> vao;
    Clock::time_point lastCleanup{};

    // --- Thread da IA ---
    std::thread worker;
    std::unique_ptr<Session> session[kModelKindCount];
    std::uint64_t sessionGeneration[kModelKindCount] = {0, 0};
    bool sessionFailed[kModelKindCount] = {false, false};

    // ---------------------------------------------------------------------------------------------- comuns
    int effectiveRate() const
    {
        if (!profileOn)
            return 0;
        return rateOverride > 0 ? rateOverride : profileRate;
    }

    void ensureWorker()
    {
        if (!worker.joinable()) {
            stop = false;
            worker = std::thread([this] { workerMain(); });
        }
    }

    void refreshPresence()
    {
        const auto now = Clock::now();
        if (presentChecked != Clock::time_point{} && now - presentChecked < std::chrono::seconds(2))
            return;
        presentChecked = now;
        for (int k = 0; k < kModelKindCount; ++k)
            presentCache[k] = modelPresent(static_cast<ModelKind>(k));
    }

    // ---------------------------------------------------------------------------------------------- thread da IA
    // Cria (ou recria) a sessao do modelo. Devolve nullptr se falhou (sessionFailed = true).
    Session* ensureSession(int k, bool forceReload = false)
    {
        DevicePref wantPref;
        std::uint64_t gen;
        bool retry;
        {
            std::lock_guard lock(m);
            wantPref = pref;
            gen = prefGeneration;
            retry = retryFlag[k];
            retryFlag[k] = false;
        }
        if (forceReload || retry || ((session[k] || sessionFailed[k]) && sessionGeneration[k] != gen)) {
            session[k].reset();
            sessionFailed[k] = false;
        }
        if (session[k])
            return session[k].get();
        if (sessionFailed[k])
            return nullptr;

        std::string err;
        std::unique_ptr<Session> s = Session::create(static_cast<ModelKind>(k), wantPref, err);
        std::lock_guard lock(m);
        ModelStats& ms = model[k];
        ms.present = presentCache[k];
        sessionGeneration[k] = gen;
        if (!s) {
            sessionFailed[k] = true;
            ms.loaded = false;
            ms.error = err;
            failedAt[k] = Clock::now();
            log::warn(std::string("IA: modelo ") + modelSpec(static_cast<ModelKind>(k)).key + " indisponível: " + err);
            return nullptr;
        }
        ms.loaded = true;
        ms.error.clear();
        ms.onGpu = s->onGpu();
        ms.device = s->deviceName();
        session[k] = std::move(s);
        log::info(std::string("IA: modelo ") + modelSpec(static_cast<ModelKind>(k)).key + " carregado em " + ms.device);
        return session[k].get();
    }

    // Roda um modelo; se a GPU falhar no meio do show (driver, device removed), cai para a CPU e repete uma vez
    bool runModel(int k, const std::uint8_t* rgb, std::vector<float>& out, double& ms)
    {
        Session* s = ensureSession(k);
        if (!s)
            return false;
        std::string err;
        if (s->run(rgb, out, err)) {
            ms = s->lastMs();
            return true;
        }
        if (!s->onGpu()) {
            std::lock_guard lock(m);
            model[k].error = err;
            return false;
        }
        log::warn("IA: falha no DirectML (" + err + "): passando para a CPU");
        session[k].reset();
        std::string err2;
        std::unique_ptr<Session> cpu = Session::create(static_cast<ModelKind>(k), DevicePref::Cpu, err2);
        {
            std::lock_guard lock(m);
            if (!cpu) {
                sessionFailed[k] = true;
                model[k].loaded = false;
                model[k].error = err2;
                failedAt[k] = Clock::now();
                return false;
            }
            model[k].onGpu = false;
            model[k].device = cpu->deviceName();
            model[k].error.clear();
        }
        session[k] = std::move(cpu);
        std::string err3;
        if (!session[k]->run(rgb, out, err3)) {
            std::lock_guard lock(m);
            model[k].error = err3;
            return false;
        }
        ms = session[k]->lastMs();
        return true;
    }

    void recordRun(int k, double ms)
    {
        ModelStats& s = model[k];
        s.lastMs = ms;
        s.avgMs = s.runs == 0 ? ms : s.avgMs * 0.85 + ms * 0.15;
        ++s.runs;
        const auto now = Clock::now();
        doneTimes.push_back(now);
        while (!doneTimes.empty() && now - doneTimes.front() > std::chrono::seconds(2))
            doneTimes.pop_front();
    }

    static void normalizeDepth(Slot& s, const std::vector<float>& raw, int w, int h)
    {
        // Faixa robusta (1% a 99%) suavizada no tempo: a escala nao oscila quando um objeto entra no quadro
        std::vector<float> tmp = raw;
        const size_t n = tmp.size();
        const size_t iLo = n / 100, iHi = n - 1 - n / 100;
        std::nth_element(tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(iLo), tmp.end());
        const float lo = tmp[iLo];
        std::nth_element(tmp.begin(), tmp.begin() + static_cast<std::ptrdiff_t>(iHi), tmp.end());
        const float hi = tmp[iHi];
        if (!s.haveRange || s.data.size() != n) {
            s.lo = lo;
            s.hi = hi;
            s.haveRange = true;
        } else {
            s.lo += (lo - s.lo) * kRangeSmooth;
            s.hi += (hi - s.hi) * kRangeSmooth;
        }
        const float span = std::max(s.hi - s.lo, 1.0e-4f);
        const bool blend = s.data.size() == n && s.w == w && s.h == h;
        if (!blend)
            s.data.assign(n, 0.0f);
        for (size_t i = 0; i < n; ++i) {
            const float d = std::clamp((raw[i] - s.lo) / span, 0.0f, 1.0f);
            s.data[i] = blend ? s.data[i] + (d - s.data[i]) * kDepthSmooth : d;
        }
    }

    static void smoothMask(Slot& s, const std::vector<float>& raw, int w, int h)
    {
        const size_t n = raw.size();
        const bool blend = s.data.size() == n && s.w == w && s.h == h;
        if (!blend)
            s.data.assign(n, 0.0f);
        s.bytes.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float a = std::clamp(raw[i], 0.0f, 1.0f);
            s.data[i] = blend ? s.data[i] + (a - s.data[i]) * kPersonSmooth : a;
            s.bytes[i] = static_cast<std::uint8_t>(std::lround(s.data[i] * 255.0f));
        }
    }

    void processJob(Job& job)
    {
        const int k = job.kind;
        const ModelSpec& spec = modelSpec(static_cast<ModelKind>(k));
        const size_t px = static_cast<size_t>(spec.inputW) * static_cast<size_t>(spec.inputH);
        std::vector<std::uint8_t> rgb(px * 3);
        for (size_t i = 0; i < px; ++i) {
            rgb[i * 3] = job.rgba[i * 4];
            rgb[i * 3 + 1] = job.rgba[i * 4 + 1];
            rgb[i * 3 + 2] = job.rgba[i * 4 + 2];
        }
        std::vector<float> raw;
        double ms = 0.0;
        const bool ok = runModel(k, rgb.data(), raw, ms);

        std::lock_guard lock(m);
        const auto it = slots.find(job.source);
        if (it == slots.end())
            return; // a fonte sumiu durante a inferencia
        Slot& s = it->second[static_cast<size_t>(k)];
        ++s.finished;
        if (!ok)
            return;
        recordRun(k, ms);
        if (k == static_cast<int>(ModelKind::Depth))
            normalizeDepth(s, raw, spec.inputW, spec.inputH);
        else
            smoothMask(s, raw, spec.inputW, spec.inputH);
        s.w = spec.inputW;
        s.h = spec.inputH;
        ++s.version;
    }

    void runSelfTest()
    {
        std::string report;
        for (int k = 0; k < kModelKindCount; ++k) {
            const ModelSpec& spec = modelSpec(static_cast<ModelKind>(k));
            {
                std::lock_guard lock(m);
                presentChecked = {};
                refreshPresence();
            }
            Session* s = ensureSession(k, true); // forca nova tentativa
            if (!s) {
                std::lock_guard lock(m);
                report += std::string(spec.label) + ": FALHOU (" + model[k].error + ")\n";
                continue;
            }
            const std::vector<std::uint8_t> rgb = makeTestScene(spec.inputW, spec.inputH);
            std::vector<float> out;
            std::string err;
            s->run(rgb.data(), out, err); // aquecimento
            const bool ok = s->run(rgb.data(), out, err);
            char line[256];
            if (!ok) {
                std::snprintf(line, sizeof(line), "%s: FALHOU (%s)\n", spec.label, err.c_str());
            } else {
                // caixa em primeiro plano x fundo
                double box = 0.0, bg = 0.0;
                size_t nb = 0, ng = 0;
                for (int y = 0; y < spec.inputH; ++y)
                    for (int x = 0; x < spec.inputW; ++x) {
                        const float v = out[static_cast<size_t>(y) * static_cast<size_t>(spec.inputW) + static_cast<size_t>(x)];
                        if (x > spec.inputW * 35 / 100 && x < spec.inputW * 65 / 100 && y > spec.inputH * 40 / 100 &&
                            y < spec.inputH * 85 / 100) {
                            box += v;
                            ++nb;
                        } else if (x < spec.inputW * 20 / 100 && y < spec.inputH * 50 / 100) {
                            bg += v;
                            ++ng;
                        }
                    }
                box /= static_cast<double>(std::max<size_t>(nb, 1));
                bg /= static_cast<double>(std::max<size_t>(ng, 1));
                std::snprintf(line, sizeof(line), "%s: OK em %s, %.1f ms (centro %.2f, fundo %.2f)\n", spec.label,
                              s->deviceName().c_str(), s->lastMs(), box, bg);
                std::lock_guard lock(m);
                recordRun(k, s->lastMs());
            }
            report += line;
        }
        std::lock_guard lock(m);
        testReport = report;
        testRunning = false;
        testDone = true;
    }

    void workerMain()
    {
        for (;;) {
            Job job;
            bool doTest = false;
            {
                std::unique_lock lock(m);
                cv.wait(lock, [&] { return stop || testRequested || !queue.empty(); });
                if (stop)
                    break;
                if (testRequested) {
                    testRequested = false;
                    doTest = true;
                } else {
                    job = std::move(queue.front());
                    queue.pop_front();
                }
            }
            try {
                if (doTest)
                    runSelfTest();
                else
                    processJob(job);
            } catch (const std::exception& e) {
                log::warn(std::string("IA: erro na thread da IA: ") + e.what());
                std::lock_guard lock(m);
                testRunning = false;
                if (!doTest) {
                    // o job nao vai mais concluir: libera a fonte para a proxima captura
                    const auto it = slots.find(job.source);
                    if (it != slots.end()) {
                        Slot& s = it->second[static_cast<size_t>(job.kind)];
                        if (s.finished < s.submitted)
                            s.finished = s.submitted;
                    }
                }
            }
        }
        // sessoes morrem na propria thread
        for (auto& s : session)
            s.reset();
    }

    // ---------------------------------------------------------------------------------------------- thread de render
    void cleanup(Clock::time_point now)
    {
        if (now - lastCleanup < std::chrono::seconds(1))
            return;
        lastCleanup = now;
        std::vector<std::uint64_t> dead;
        for (auto& [id, e] : entries)
            if (std::chrono::duration<double>(now - e.lastUsed).count() > kIdleSeconds)
                dead.push_back(id);
        for (std::uint64_t id : dead) {
            destroyEntry(entries[id]);
            entries.erase(id);
        }
        std::lock_guard lock(m);
        for (std::uint64_t id : dead)
            slots.erase(id);
        activeSources = static_cast<int>(entries.size());
    }

    void ensureCaptureGl()
    {
        if (!capShader)
            capShader = std::make_unique<Shader>(fx::kFxVertexSource, kCaptureFrag);
        if (!vao)
            vao = std::make_unique<fx::ScreenVao>();
    }

    // Captura (reduz + le por PBO) e entrega ao worker quando o PBO ficar pronto; envia o resultado novo para a textura
    void service(Entry& e, std::uint64_t source, int k, GLuint inputTex, bool topOrigin, Clock::time_point now, int rate)
    {
        const ModelSpec& spec = modelSpec(static_cast<ModelKind>(k));
        Capture& c = e.cap[static_cast<size_t>(k)];

        // 1) PBO pronto -> job
        if (c.pending && c.fence) {
            const GLenum r = glClientWaitSync(c.fence, 0, 0);
            if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED || r == GL_WAIT_FAILED) {
                glDeleteSync(c.fence);
                c.fence = nullptr;
                c.pending = false;
                Job job;
                job.source = source;
                job.kind = k;
                const size_t bytes = static_cast<size_t>(spec.inputW) * static_cast<size_t>(spec.inputH) * 4;
                job.rgba.resize(bytes);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, c.pbo);
                if (const void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(bytes), GL_MAP_READ_BIT)) {
                    std::memcpy(job.rgba.data(), p, bytes);
                    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                    {
                        std::lock_guard lock(m);
                        ++slots[source][static_cast<size_t>(k)].submitted;
                        queue.push_back(std::move(job));
                    }
                    cv.notify_one();
                } else {
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                }
            }
        }

        // 2) hora de uma captura nova? (sem job em voo e com o intervalo cumprido)
        bool inflight;
        {
            std::lock_guard lock(m);
            const Slot& s = slots[source][static_cast<size_t>(k)];
            inflight = s.submitted != s.finished;
        }
        const double interval = 1.0 / static_cast<double>(std::max(rate, 1));
        if (!c.pending && !inflight && (!c.haveLast || std::chrono::duration<double>(now - c.last).count() >= interval * 0.9)) {
            ensureCaptureGl();
            if (!c.fbo || c.fbo->width() != spec.inputW || c.fbo->height() != spec.inputH)
                c.fbo = std::make_unique<Fbo>(spec.inputW, spec.inputH);
            if (!c.pbo) {
                glGenBuffers(1, &c.pbo);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, c.pbo);
                glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(spec.inputW) * spec.inputH * 4, nullptr, GL_STREAM_READ);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            }
            GLint prevViewport[4];
            glGetIntegerv(GL_VIEWPORT, prevViewport);
            GLint prevDraw = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);

            fx::bindOutput(*c.fbo, spec.inputW, spec.inputH);
            capShader->use();
            capShader->setInt("uTex", 0);
            capShader->setInt("uTop", topOrigin ? 1 : 0);
            capShader->setVec2("uStep", 1.0f / static_cast<float>(spec.inputW), 1.0f / static_cast<float>(spec.inputH));
            fx::bindTexture(0, inputTex);
            vao->draw();
            fx::bindTexture(0, 0);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, c.fbo->id());
            glBindBuffer(GL_PIXEL_PACK_BUFFER, c.pbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, spec.inputW, spec.inputH, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            c.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            c.pending = true;
            c.last = now;
            c.haveLast = true;

            glUseProgram(0);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevDraw));
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        }

        // 3) resultado novo -> textura
        std::vector<float> fdata;
        std::vector<std::uint8_t> bdata;
        int rw = 0, rh = 0;
        std::uint64_t version = 0;
        {
            std::lock_guard lock(m);
            const Slot& s = slots[source][static_cast<size_t>(k)];
            if (s.version != e.uploaded[static_cast<size_t>(k)] && !s.data.empty()) {
                version = s.version;
                rw = s.w;
                rh = s.h;
                if (k == static_cast<int>(ModelKind::Depth))
                    fdata = s.data;
                else
                    bdata = s.bytes;
            }
        }
        if (version != 0) {
            GLuint& tex = e.tex[static_cast<size_t>(k)];
            GLint prevTex = 0, prevAlign = 4;
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            const bool depth = k == static_cast<int>(ModelKind::Depth);
            if (!tex) {
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (depth)
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, rw, rh, 0, GL_RED, GL_FLOAT, nullptr);
                else
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, rw, rh, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
                e.texW[static_cast<size_t>(k)] = rw;
                e.texH[static_cast<size_t>(k)] = rh;
            } else {
                glBindTexture(GL_TEXTURE_2D, tex);
            }
            if (depth)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh, GL_RED, GL_FLOAT, fdata.data());
            else
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh, GL_RED, GL_UNSIGNED_BYTE, bdata.data());
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
            glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
            e.uploaded[static_cast<size_t>(k)] = version;
        }
    }
};

AiEngine::AiEngine() : impl_(new Impl()) {}

AiEngine::~AiEngine()
{
    {
        std::lock_guard lock(impl_->m);
        impl_->stop = true;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable())
        impl_->worker.join();
    delete impl_;
}

AiEngine& AiEngine::instance()
{
    // Nunca destruido: o destrutor estatico rodaria depois do contexto GL
    static AiEngine* engine = new AiEngine();
    return *engine;
}

std::string aiStatusText(const AiFrame& frame)
{
    switch (frame.state) {
    case AiState::Off: return "IA desligada no perfil Eco";
    case AiState::Unavailable: return "IA indisponível: " + frame.message;
    case AiState::Warming: return frame.message.empty() ? "IA carregando…" : frame.message;
    case AiState::Ready: return {};
    }
    return {};
}

void AiEngine::setProfileRate(int fps)
{
    std::lock_guard lock(impl_->m);
    impl_->profileOn = fps > 0;
    impl_->profileRate = std::max(fps, 0);
}

void AiEngine::setRateOverride(int fps)
{
    std::lock_guard lock(impl_->m);
    impl_->rateOverride = std::clamp(fps, 0, 120);
}

void AiEngine::setDevicePref(DevicePref pref)
{
    std::lock_guard lock(impl_->m);
    if (impl_->pref != pref) {
        impl_->pref = pref;
        ++impl_->prefGeneration;
    }
}

bool AiEngine::enabled() const
{
    std::lock_guard lock(impl_->m);
    return impl_->profileOn;
}

int AiEngine::rate() const
{
    std::lock_guard lock(impl_->m);
    return impl_->effectiveRate();
}

void AiEngine::refreshModelPresence()
{
    std::lock_guard lock(impl_->m);
    impl_->presentChecked = {};
    impl_->refreshPresence();
}

void AiEngine::setOverride(std::uint64_t sourceId, GLuint depthTex, GLuint maskTex)
{
    std::lock_guard lock(impl_->m);
    if (depthTex == 0 && maskTex == 0)
        impl_->overrides.erase(sourceId);
    else
        impl_->overrides[sourceId] = {depthTex, maskTex};
}

AiFrame AiEngine::acquire(std::uint64_t sourceId, std::uint64_t /*frameIndex*/, GLuint inputTex, int w, int h,
                          bool topOrigin, unsigned need, bool eco)
{
    Impl& im = *impl_;
    AiFrame f;

    int rate;
    {
        std::lock_guard lock(im.m);
        const auto ov = im.overrides.find(sourceId);
        if (ov != im.overrides.end()) {
            f.depth = (need & kNeedDepth) ? ov->second.first : 0;
            f.mask = (need & kNeedPerson) ? ov->second.second : 0;
            f.depthW = f.depthH = f.maskW = f.maskH = 1;
            const bool ok = ((need & kNeedDepth) == 0 || f.depth != 0) && ((need & kNeedPerson) == 0 || f.mask != 0);
            f.state = ok ? AiState::Ready : AiState::Warming;
            return f;
        }
        rate = im.effectiveRate();
    }
    if (eco || rate <= 0) {
        f.state = AiState::Off;
        f.message = "IA desligada no perfil Eco";
        return f;
    }
    if (inputTex == 0 || w <= 0 || h <= 0 || need == 0) {
        f.state = AiState::Warming;
        return f;
    }

    const auto now = Clock::now();
    im.cleanup(now);
    Entry& e = im.entries[sourceId];
    e.lastUsed = now;
    {
        std::lock_guard lock(im.m);
        im.slots[sourceId]; // garante o slot
        im.activeSources = static_cast<int>(im.entries.size());
        im.refreshPresence();
    }

    std::string unavailable;
    for (int k = 0; k < kModelKindCount; ++k) {
        if (!(need & (1u << k)))
            continue;
        const ModelSpec& spec = modelSpec(static_cast<ModelKind>(k));
        {
            std::lock_guard lock(im.m);
            if (!im.presentCache[k]) {
                unavailable = std::string("modelo ausente (") + spec.fileName + ")";
                continue;
            }
            if (!im.model[k].loaded && !im.model[k].error.empty()) {
                if (now - im.failedAt[k] < std::chrono::seconds(15)) {
                    unavailable = im.model[k].error;
                    continue;
                }
                im.retryFlag[k] = true; // tenta de novo (o arquivo pode ter sido colocado na pasta)
                im.failedAt[k] = now;
            }
        }
        im.ensureWorker();
        im.service(e, sourceId, k, inputTex, topOrigin, now, rate);
    }

    // Texturas disponiveis
    if (need & kNeedDepth) {
        f.depth = e.tex[static_cast<size_t>(ModelKind::Depth)];
        f.depthW = e.texW[static_cast<size_t>(ModelKind::Depth)];
        f.depthH = e.texH[static_cast<size_t>(ModelKind::Depth)];
    }
    if (need & kNeedPerson) {
        f.mask = e.tex[static_cast<size_t>(ModelKind::Person)];
        f.maskW = e.texW[static_cast<size_t>(ModelKind::Person)];
        f.maskH = e.texH[static_cast<size_t>(ModelKind::Person)];
    }
    const bool ready = ((need & kNeedDepth) == 0 || f.depth != 0) && ((need & kNeedPerson) == 0 || f.mask != 0);
    if (ready) {
        f.state = AiState::Ready;
    } else if (!unavailable.empty()) {
        f.state = AiState::Unavailable;
        f.message = unavailable;
    } else {
        f.state = AiState::Warming;
        f.message = "IA carregando…";
    }
    return f;
}

AiStats AiEngine::stats() const
{
    Impl& im = *impl_;
    AiStats s;
    std::lock_guard lock(im.m);
    im.refreshPresence();
    s.enabled = im.profileOn;
    s.rate = im.effectiveRate();
    const RuntimeSnapshot rs = peekRuntime();
    s.runtimeTried = rs.tried;
    s.runtimeLoaded = rs.info.loaded;
    s.runtimeVersion = rs.info.version;
    s.runtimeError = rs.info.error;
    s.gpuName = rs.info.gpuAvailable ? rs.info.gpuName : std::string();
    s.activeSources = im.activeSources;
    const auto now = Clock::now();
    int count = 0;
    for (const auto& t : im.doneTimes)
        if (now - t <= std::chrono::seconds(1))
            ++count;
    s.inferencesPerSec = static_cast<float>(count);
    for (int k = 0; k < kModelKindCount; ++k) {
        s.models[k] = im.model[k];
        s.models[k].present = im.presentCache[k];
    }
    s.testRunning = im.testRunning;
    s.testDone = im.testDone;
    s.testReport = im.testReport;
    return s;
}

void AiEngine::requestSelfTest()
{
    {
        std::lock_guard lock(impl_->m);
        if (impl_->testRunning)
            return;
        impl_->testRunning = true;
        impl_->testDone = false;
        impl_->testRequested = true;
        impl_->testReport.clear();
        impl_->ensureWorker();
    }
    impl_->cv.notify_all();
}

void AiEngine::shutdown()
{
    Impl& im = *impl_;
    {
        std::lock_guard lock(im.m);
        im.stop = true;
    }
    im.cv.notify_all();
    if (im.worker.joinable())
        im.worker.join();
    for (auto& [id, e] : im.entries)
        destroyEntry(e);
    im.entries.clear();
    im.capShader.reset();
    im.vao.reset();
    std::lock_guard lock(im.m);
    im.slots.clear();
    im.queue.clear();
    im.activeSources = 0;
    im.stop = false;
}

} // namespace bsl::ai
