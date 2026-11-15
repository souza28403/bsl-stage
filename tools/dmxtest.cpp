#include "control/ActionRegistry.h"
#include "control/KeyService.h"
#include "core/Composition.h"
#include "core/Generators.h"
#include "dmx/DmxEngine.h"
#include "dmx/DmxExport.h"
#include "dmx/DmxMap.h"
#include "dmx/DmxMerge.h"
#include "dmx/DmxNet.h"
#include "dmx/DmxProtocol.h"
#include "dmx/DmxService.h"
#include "io/ProjectFile.h"
#include "output/OutputSetup.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>

#include "platform/Socket.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace bsl;
using namespace bsl::dmx;

namespace {

unsigned long testProcessId()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

int g_failures = 0;
int g_checks = 0;
int g_notTested = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::printf("  FALHOU (linha %d): %s\n", __LINE__, #cond);                     \
        }                                                                                  \
    } while (0)

void section(const char* name)
{
    std::printf("[%s]\n", name);
}

bool closeTo(double a, double b, double tol = 1.0e-3)
{
    return std::fabs(a - b) <= tol;
}

void sleepMs(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

Composition makeComp()
{
    Composition comp = Composition::createDefault(3, 8);
    comp.width = 64;
    comp.height = 64;
    for (const auto& [l, c] : {std::pair{0, 0}, {0, 1}, {1, 0}, {1, 1}, {1, 2}, {2, 0}})
        comp.setClip(l, c, Clip::generator(kSolidColorId));
    return comp;
}

struct Fake
{
    Snapshot snap;

    UniverseView& uv(int universe)
    {
        if (UniverseView* v = snap.find(universe))
            return *v;
        UniverseView v;
        v.universe = universe;
        v.everSeen = true;
        v.active = true;
        v.lastFrame = 1.0;
        snap.universes.push_back(v);
        std::sort(snap.universes.begin(), snap.universes.end(),
                  [](const UniverseView& a, const UniverseView& b) { return a.universe < b.universe; });
        return *snap.find(universe);
    }
    void set(int universe, int channel, int value) { uv(universe).data[static_cast<size_t>(channel - 1)] = static_cast<std::uint8_t>(value); }
    void set16(int universe, int channel, int value)
    {
        set(universe, channel, value >> 8);
        set(universe, channel + 1, value & 0xFF);
    }
    void release(int universe, bool on)
    {
        UniverseView& v = uv(universe);
        v.active = !on;
        v.lost = on;
        v.released = on;
    }
};

struct Rig
{
    Composition comp = makeComp();
    control::KeyService keys;
    Engine engine;
    control::SelectionRef sel;
    Fake fake;

    int layerId(int index) const { return comp.layers[static_cast<size_t>(index)].id; }
    int apply() { return engine.apply(comp, keys, sel, fake.snap); }

    Block& layerBlock(int layerIndex, LayerMode mode, int universe = 1, int channel = 1)
    {
        Block b;
        b.kind = BlockKind::Layer;
        b.layerId = layerId(layerIndex);
        b.mode = mode;
        b.universe = universe;
        b.channel = channel;
        return comp.dmx.addBlock(std::move(b));
    }
    Block& masterBlock(int universe, int channel)
    {
        Block b;
        b.kind = BlockKind::Master;
        b.universe = universe;
        b.channel = channel;
        return comp.dmx.addBlock(std::move(b));
    }
};

void testPackets()
{
    section("pacotes Art-Net");
    std::vector<std::uint8_t> payload(512);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>(i * 7);
    const std::vector<std::uint8_t> art = buildArtDmx(0x1234, 9, payload.data(), 512);
    CHECK(art.size() == 18 + 512);
    CHECK(std::memcmp(art.data(), "Art-Net\0", 8) == 0);
    CHECK(art[8] == 0x00 && art[9] == 0x50);
    CHECK(art[10] == 0 && art[11] == 14);
    CHECK(art[12] == 9);
    CHECK(art[14] == 0x34 && art[15] == 0x12);
    CHECK(art[16] == 0x02 && art[17] == 0x00);
    CHECK(artNetOpCode(art.data(), art.size()) == kArtOpDmx);
    DmxFrame f;
    CHECK(parseArtDmx(art.data(), art.size(), f));
    CHECK(f.protocol == Protocol::ArtNet && f.universe == 0x1234 && f.sequence == 9 && f.length == 512);
    CHECK(std::equal(payload.begin(), payload.end(), f.data.begin()));
    const std::vector<std::uint8_t> big = buildArtDmx(0x7FFF, 1, payload.data(), 4);
    CHECK(parseArtDmx(big.data(), big.size(), f) && f.universe == 0x7FFF && f.length == 4 && f.data[4] == 0);
    std::vector<std::uint8_t> bad = art;
    bad[0] = 'X';
    CHECK(!parseArtDmx(bad.data(), bad.size(), f));
    CHECK(artNetOpCode(bad.data(), bad.size()) == 0);
    CHECK(!parseArtDmx(art.data(), 17, f));
    CHECK(!parseArtDmx(art.data(), 100, f));
    bad = art;
    bad[11] = 3;
    CHECK(!parseArtDmx(bad.data(), bad.size(), f));
    bad = art;
    bad[16] = 0x10;
    CHECK(!parseArtDmx(bad.data(), bad.size(), f));

    const std::vector<std::uint8_t> poll = buildArtPoll(0x02, 0x10);
    ArtPollInfo pi;
    CHECK(poll.size() == 14 && artNetOpCode(poll.data(), poll.size()) == kArtOpPoll);
    CHECK(parseArtPoll(poll.data(), poll.size(), pi) && pi.flags == 0x02 && pi.diagPriority == 0x10);
    CHECK(!parseArtPoll(art.data(), art.size(), pi));

    ArtNodeInfo node;
    node.ip = {192, 168, 1, 77};
    node.mac = {1, 2, 3, 4, 5, 6};
    node.net = 1;
    node.sub = 2;
    node.universes = {0, 1, 2, 15, 9};
    node.bindIndex = 3;
    node.receiving = true;
    const std::vector<std::uint8_t> reply = buildArtPollReply(node);
    CHECK(static_cast<int>(reply.size()) == kArtPollReplySize);
    CHECK(reply[8] == 0x00 && reply[9] == 0x21);
    CHECK(reply[10] == 192 && reply[13] == 77);
    CHECK(reply[14] == 0x36 && reply[15] == 0x19);
    CHECK(reply[201] == 1 && reply[206] == 6);
    CHECK(reply[211] == 3);
    ArtPollReplyView rv;
    CHECK(parseArtPollReply(reply.data(), reply.size(), rv));
    CHECK(rv.shortName == "BSL Stage" && rv.longName.find("BSL Stage") == 0);
    CHECK(rv.net == 1 && rv.sub == 2 && rv.numPorts == 4 && rv.bindIndex == 3);
    CHECK(rv.portTypes[0] == 0x80 && rv.portTypes[3] == 0x80);
    CHECK(rv.swOut[0] == 0 && rv.swOut[1] == 1 && rv.swOut[2] == 2 && rv.swOut[3] == 15);
    CHECK(rv.ip[0] == 192);

    section("pacotes sACN (E1.31)");
    std::array<std::uint8_t, 16> cid{};
    for (size_t i = 0; i < cid.size(); ++i)
        cid[i] = static_cast<std::uint8_t>(0xA0 + i);
    const std::vector<std::uint8_t> sacn = buildSacnData(300, cid, "Mesa de teste", 150, 41, payload.data(), 512);
    CHECK(sacn.size() == 638);
    CHECK(sacn[0] == 0x00 && sacn[1] == 0x10);
    CHECK(std::memcmp(sacn.data() + 4, "ASC-E1.17\0\0\0", 12) == 0);
    CHECK((sacn[16] & 0xF0) == 0x70 && (((sacn[16] & 0x0F) << 8) | sacn[17]) == 638 - 16);
    CHECK(sacn[21] == 0x04);
    CHECK((((sacn[38] & 0x0F) << 8) | sacn[39]) == 638 - 38);
    CHECK(sacn[43] == 0x02);
    CHECK(sacn[108] == 150 && sacn[111] == 41);
    CHECK(sacn[113] == 0x01 && sacn[114] == 0x2C);
    CHECK((((sacn[115] & 0x0F) << 8) | sacn[116]) == 638 - 115);
    CHECK(sacn[117] == 0x02 && sacn[118] == 0xA1 && sacn[121] == 0x00 && sacn[122] == 0x01);
    CHECK(sacn[123] == 0x02 && sacn[124] == 0x01);
    CHECK(sacn[125] == 0x00);
    CHECK(sacn[126] == payload[0] && sacn[637] == payload[511]);
    int startCode = -1;
    CHECK(parseSacnData(sacn.data(), sacn.size(), f, &startCode));
    CHECK(f.protocol == Protocol::Sacn && f.universe == 300 && f.priority == 150 && f.sequence == 41 && f.length == 512);
    CHECK(f.cid == cid && f.sourceName == "Mesa de teste" && !f.terminated && startCode == 0);
    CHECK(std::equal(payload.begin(), payload.end(), f.data.begin()));
    const std::vector<std::uint8_t> term = buildSacnData(1, cid, "x", 100, 1, payload.data(), 8, true);
    CHECK(parseSacnData(term.data(), term.size(), f) && f.terminated && f.length == 8);
    std::vector<std::uint8_t> dd = sacn;
    dd[125] = 0xDD;
    CHECK(!parseSacnData(dd.data(), dd.size(), f, &startCode) && startCode == 0xDD);
    CHECK(!parseSacnData(sacn.data(), 100, f));
    std::vector<std::uint8_t> badAcn = sacn;
    badAcn[5] = 'X';
    CHECK(!parseSacnData(badAcn.data(), badAcn.size(), f));
    std::vector<std::uint8_t> badVec = sacn;
    badVec[21] = 0x08;
    CHECK(!parseSacnData(badVec.data(), badVec.size(), f));
    const std::vector<std::uint8_t> u0 = buildSacnData(0, cid, "x", 100, 1, payload.data(), 4);
    CHECK(!parseSacnData(u0.data(), u0.size(), f));
    std::vector<std::uint8_t> shortCount = sacn;
    shortCount[124] = 0xFF;
    shortCount[123] = 0x01;
    CHECK(!parseSacnData(shortCount.data(), 200, f));

    CHECK(sacnMulticastAddress(1) == 0xEFFF0001u);
    CHECK(ipToString(sacnMulticastAddress(300)) == "239.255.1.44");

    section("lista de universos");
    CHECK((parseUniverseList("1-4, 10;12 13-14") == std::vector<int>{1, 2, 3, 4, 10, 12, 13, 14}));
    CHECK((parseUniverseList("5,5,3") == std::vector<int>{3, 5}));
    CHECK((parseUniverseList("0,70000,abc,7") == std::vector<int>{7}));
    CHECK(parseUniverseList("").empty());
    CHECK(parseUniverseList("1-100000", 10).size() == 10);
    CHECK(formatUniverseList({1, 2, 3, 4, 10, 12, 13, 15}) == "1-4, 10, 12, 13, 15");
}

DmxFrame artFrame(std::initializer_list<std::pair<int, int>> values, std::uint8_t seq = 0)
{
    DmxFrame f;
    f.protocol = Protocol::ArtNet;
    f.universe = 0;
    f.sequence = seq;
    f.length = 512;
    for (const auto& [ch, v] : values)
        f.data[static_cast<size_t>(ch - 1)] = static_cast<std::uint8_t>(v);
    return f;
}

DmxFrame sacnFrame(std::uint8_t cidByte, std::initializer_list<std::pair<int, int>> values, std::uint8_t priority = 100,
                   std::uint8_t seq = 1)
{
    DmxFrame f;
    f.protocol = Protocol::Sacn;
    f.universe = 1;
    f.cid.fill(cidByte);
    f.priority = priority;
    f.sequence = seq;
    f.length = 512;
    f.sourceName = "src" + std::to_string(cidByte);
    for (const auto& [ch, v] : values)
        f.data[static_cast<size_t>(ch - 1)] = static_cast<std::uint8_t>(v);
    return f;
}

void testMerge()
{
    section("mistura HTP");
    {
        UniverseMerger m;
        MergeConfig c;
        c.mode = MergeMode::Htp;
        m.configure(c);
        CHECK(!m.active() && !m.everSeen());
        CHECK(m.ingest(sacnFrame(1, {{1, 100}, {2, 10}}), 0x0A000001, 1.0));
        CHECK(m.ingest(sacnFrame(2, {{1, 50}, {2, 200}}), 0x0A000002, 1.0));
        CHECK(m.output()[0] == 100 && m.output()[1] == 200 && m.output()[2] == 0);
        CHECK(m.liveSources() == 2 && m.active());
        CHECK(m.ingest(sacnFrame(1, {{1, 20}}, 100, 2), 0x0A000001, 1.1));
        CHECK(m.output()[0] == 50 && m.output()[1] == 200);
        DmxFrame shortFrame = sacnFrame(2, {{1, 5}}, 100, 2);
        shortFrame.length = 1;
        CHECK(m.ingest(shortFrame, 0x0A000002, 1.2));
        CHECK(m.output()[0] == 20 && m.output()[1] == 0);
    }
    section("mistura LTP");
    {
        UniverseMerger m;
        MergeConfig c;
        c.mode = MergeMode::Ltp;
        m.configure(c);
        m.ingest(sacnFrame(1, {{1, 100}}), 1, 1.0);
        m.ingest(sacnFrame(2, {{1, 50}}), 2, 1.0);
        CHECK(m.output()[0] == 50);
        m.ingest(sacnFrame(1, {{1, 80}}, 100, 2), 1, 1.1);
        CHECK(m.output()[0] == 80);
        m.ingest(sacnFrame(2, {{1, 50}}, 100, 2), 2, 1.2);
        CHECK(m.output()[0] == 80);
        m.ingest(sacnFrame(2, {{1, 51}}, 100, 3), 2, 1.3);
        CHECK(m.output()[0] == 51);
    }
    section("prioridade (sACN) e Art-Net");
    {
        UniverseMerger m;
        MergeConfig c;
        c.mode = MergeMode::Htp;
        c.artNetPriority = 100;
        m.configure(c);
        m.ingest(artFrame({{1, 255}, {2, 255}}), 0x0A000009, 1.0);
        m.ingest(sacnFrame(1, {{1, 10}}, 150), 1, 1.0);
        CHECK(m.output()[0] == 10 && m.output()[1] == 0);
        const std::vector<SourceStat> st = m.sources();
        CHECK(st.size() == 2);
        int inMerge = 0;
        for (const SourceStat& s : st)
            inMerge += s.inMerge ? 1 : 0;
        CHECK(inMerge == 1);
        m.tick(1.0 + 10.0);
        CHECK(!m.active());
        m.ingest(artFrame({{1, 77}}, 2), 0x0A000009, 12.0);
        CHECK(m.output()[0] == 77 && m.active());
        MergeConfig c2 = c;
        c2.artNetPriority = 200;
        UniverseMerger m2;
        m2.configure(c2);
        m2.ingest(artFrame({{1, 9}}), 5, 1.0);
        m2.ingest(sacnFrame(3, {{1, 255}}, 150), 6, 1.0);
        CHECK(m2.output()[0] == 9);
    }
    section("sequencia e fim de transmissao");
    {
        UniverseMerger m;
        m.configure(MergeConfig{});
        CHECK(m.ingest(sacnFrame(1, {{1, 10}}, 100, 10), 1, 1.0));
        CHECK(!m.ingest(sacnFrame(1, {{1, 99}}, 100, 10), 1, 1.01));
        CHECK(!m.ingest(sacnFrame(1, {{1, 98}}, 100, 5), 1, 1.02));
        CHECK(m.output()[0] == 10);
        CHECK(m.ingest(sacnFrame(1, {{1, 11}}, 100, 11), 1, 1.03));
        CHECK(!m.ingest(sacnFrame(1, {{1, 12}}, 100, 4), 1, 1.04));
        CHECK(m.ingest(sacnFrame(1, {{1, 30}}, 100, 200), 1, 1.05));
        UniverseMerger a;
        a.configure(MergeConfig{});
        CHECK(a.ingest(artFrame({{1, 1}}, 0), 7, 1.0));
        CHECK(a.ingest(artFrame({{1, 2}}, 0), 7, 1.01));
        UniverseMerger t;
        t.configure(MergeConfig{});
        t.ingest(sacnFrame(1, {{1, 200}}, 100, 1), 1, 1.0);
        DmxFrame stop = sacnFrame(1, {{1, 200}}, 100, 2);
        stop.terminated = true;
        t.ingest(stop, 1, 1.1);
        CHECK(!t.active() && t.lost());
    }
    section("timeout: manter e soltar");
    {
        MergeConfig hold;
        hold.timeoutSec = 2.5;
        hold.loss = LossPolicy::Hold;
        UniverseMerger m;
        m.configure(hold);
        m.ingest(sacnFrame(1, {{1, 123}, {512, 7}}), 1, 10.0);
        m.tick(12.0);
        CHECK(m.active() && !m.lost());
        m.tick(12.6);
        CHECK(!m.active() && m.lost());
        CHECK(m.output()[0] == 123 && m.output()[511] == 7);
        CHECK(m.ingest(sacnFrame(1, {{1, 5}}, 100, 1), 1, 20.0));
        CHECK(m.active() && m.output()[0] == 5);

        MergeConfig rel = hold;
        rel.loss = LossPolicy::Release;
        UniverseMerger r;
        r.configure(rel);
        r.ingest(sacnFrame(1, {{1, 123}}), 1, 10.0);
        CHECK(r.output()[0] == 123);
        r.tick(13.0);
        CHECK(!r.active() && r.lost());
        CHECK(r.output()[0] == 0);
        UniverseMerger two;
        two.configure(hold);
        two.ingest(sacnFrame(1, {{1, 10}}), 1, 1.0);
        two.ingest(sacnFrame(2, {{1, 20}}), 2, 3.0);
        two.tick(4.0);
        CHECK(two.active() && two.liveSources() == 1 && two.output()[0] == 20);
    }
}

void testProfiles()
{
    section("personalidades e contas");
    CHECK(layerChannelCount(LayerMode::Basic, 0) == 2);
    CHECK(layerChannelCount(LayerMode::Standard, 0) == 4);
    CHECK(layerChannelCount(LayerMode::Full, 0) == 8);
    CHECK(layerChannelCount(LayerMode::Full16, 0) == 12);
    CHECK(layerChannelCount(LayerMode::Full, 3) == 11);
    CHECK(layerChannelCount(LayerMode::Full, 99) == 8 + kMaxFxSlots);
    const std::vector<ChannelRow> full16 = layerRows(LayerMode::Full16, 2);
    CHECK(full16.size() == 8 + 2);
    CHECK(full16[4].func == Func::PosX && full16[4].offset == 5 && full16[4].span == 2);
    CHECK(full16[7].func == Func::Rotation && full16[7].offset == 11);
    CHECK(full16[8].func == Func::Fx && full16[8].offset == 13 && full16[8].fxIndex == 0 && full16[8].key == "BSL_FX1");
    int total = 0;
    for (const ChannelRow& r : full16)
        total += r.span;
    CHECK(total == layerChannelCount(LayerMode::Full16, 2));
    CHECK(masterRows().size() == 3 && masterChannelCount() == 3);
    CHECK(personalityName(LayerMode::Full16, 4) == "Full 16bit FX4" && personalityName(LayerMode::Basic, 0) == "Basic");

    CHECK(closeTo(dmxCentered(128, 255), 0.0) && closeTo(dmxCentered(0, 255), -1.0) && closeTo(dmxCentered(255, 255), 1.0));
    CHECK(closeTo(dmxCentered(32768, 65535), 0.0) && closeTo(dmxCentered(65535, 65535), 1.0) && closeTo(dmxCentered(0, 65535), -1.0));
    CHECK(closeTo(dmxScaleOf(128, 255), 1.0) && closeTo(dmxScaleOf(255, 255), 4.0) && closeTo(dmxScaleOf(0, 255), 0.0));
    CHECK(closeTo(dmxScaleOf(32768, 65535), 1.0) && closeTo(dmxScaleOf(65535, 65535), 4.0));
    CHECK(closeTo(dmxRotationOf(255, 255), 180.0) && closeTo(dmxRotationOf(0, 255), -180.0) && closeTo(dmxRotationOf(128, 255), 0.0));
    CHECK(dmxBlendOf(0, 255) == 0 && dmxBlendOf(28, 255) == 1 && dmxBlendOf(255, 255) == 8 && dmxBlendOf(65535, 65535) == 8);
    CHECK(kDmxBlendModeCount == 9 && kBlendModeCount == 11 && dmxBlendOf(200, 255) == 7);
    CHECK(dmxClipRange(0) == 0 && dmxClipRange(9) == 0 && dmxClipRange(10) == 1 && dmxClipRange(25) == 2 && dmxClipRange(255) == 25);
    CHECK(dmxDefaultRaw(Func::Speed, 255) == 128 && dmxDefaultRaw(Func::PosX, 65535) == 32768 && dmxDefaultRaw(Func::MasterOpacity, 255) == 255);
    CHECK(clipRanges().size() == 2 + kClipMaxColumns && clipRanges().back().to == 255);
}

void testFixtureOpacityAndTransform()
{
    section("fixture: opacidade, transform 16 bits e so aplica quando muda");
    Rig r;
    r.layerBlock(0, LayerMode::Full16);
    r.fake.set(1, 1, 255);
    r.fake.set16(1, 5, 0x8000);
    r.fake.set16(1, 7, 0x8000);
    r.fake.set16(1, 9, 0x8000);
    r.fake.set16(1, 11, 0x8000);
    r.fake.set(1, 3, 128);
    r.apply();
    Layer& layer = r.comp.layers[0];
    CHECK(closeTo(layer.opacity, 1.0));
    CHECK(layer.transform.isIdentity(64, 64));

    r.fake.set16(1, 5, 0xFFFF);
    r.apply();
    CHECK(closeTo(layer.transform.x, 64.0, 0.01));
    r.fake.set16(1, 5, 0x0000);
    r.apply();
    CHECK(closeTo(layer.transform.x, -64.0, 0.01));
    r.fake.set16(1, 5, 0x8000);
    r.fake.set16(1, 7, 0xC000);
    r.apply();
    CHECK(closeTo(layer.transform.x, 0.0, 0.01));
    CHECK(closeTo(layer.transform.y, 32.0, 0.05));
    r.fake.set16(1, 5, 0x8000 + 0x4000);
    r.apply();
    const float x1 = layer.transform.x;
    r.fake.set16(1, 5, 0x8000 + 0x4001);
    r.apply();
    CHECK(layer.transform.x > x1 && layer.transform.x - x1 < 0.01f);

    r.fake.set16(1, 5, 0x8000);
    r.fake.set16(1, 7, 0x8000);
    r.apply();
    CHECK(layer.transform.isIdentity(64, 64));
    r.fake.set16(1, 9, 0xC000);
    r.apply();
    CHECK(closeTo(layer.transform.w, 160.0, 0.1) && closeTo(layer.transform.h, 160.0, 0.1));
    CHECK(closeTo(layer.transform.x, 32.0 - 80.0, 0.1));
    r.fake.set16(1, 9, 0x8000);
    r.apply();
    CHECK(layer.transform.w == 0.0f && layer.transform.h == 0.0f && layer.transform.isIdentity(64, 64));

    r.fake.set16(1, 11, 0xFFFF);
    r.apply();
    CHECK(closeTo(layer.transform.rotation, 180.0, 0.01));
    r.fake.set16(1, 11, 0xC000);
    r.apply();
    CHECK(closeTo(layer.transform.rotation, 90.0, 0.05));

    layer.opacity = 0.25f;
    r.apply();
    CHECK(closeTo(layer.opacity, 0.25));
    r.fake.set(1, 1, 51);
    r.apply();
    CHECK(closeTo(layer.opacity, 0.2));
    r.fake.set(1, 100, 255);
    CHECK(r.apply() == 0);
}

void testFixtureClip()
{
    section("fixture: clip por faixa de valor (limiar na entrada da faixa)");
    Rig r;
    r.layerBlock(0, LayerMode::Standard);
    r.fake.set(1, 2, 0);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == -1);
    r.fake.set(1, 2, 25);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == 0);
    r.fake.set(1, 2, 29);
    r.comp.stopLayer(0);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == -1);
    r.fake.set(1, 2, 31);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == 1);
    r.fake.set(1, 2, 14);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == -1);
    r.fake.set(1, 2, 3);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == -1);
    r.fake.set(1, 2, 30);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == 1);
    r.fake.set(1, 2, 200);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == 1);

    Rig fresh;
    fresh.layerBlock(0, LayerMode::Standard);
    fresh.fake.set(1, 2, 25);
    fresh.apply();
    CHECK(fresh.comp.layers[0].activeColumn == -1);
    fresh.fake.set(1, 2, 35);
    fresh.apply();
    CHECK(fresh.comp.layers[0].activeColumn == 1);

    fresh.comp.stopLayer(0);
    fresh.comp.dmx.blocks[0].channel = 20;
    fresh.fake.set(1, 21, 25);
    fresh.apply();
    CHECK(fresh.comp.layers[0].activeColumn == -1);
    fresh.fake.set(1, 21, 35);
    fresh.apply();
    CHECK(fresh.comp.layers[0].activeColumn == 1);
}

void testFixtureSpeedBlendFx()
{
    section("fixture: velocidade, blend e canais de efeito");
    Rig r;
    Block& b = r.layerBlock(1, LayerMode::Standard);
    r.comp.layers[1].effects.add("native:blur");
    FxSlot radius;
    radius.effect = 0;
    radius.param = "radius";
    FxSlot quality;
    quality.effect = 0;
    quality.param = "quality";
    b.fx = {radius, quality};
    CHECK(b.channelCount() == 6 && b.lastChannel() == 6);

    r.fake.set(1, 2, 0);
    r.fake.set(1, 3, 128);
    r.apply();
    r.fake.set(1, 2, 20);
    r.apply();
    CHECK(r.comp.layers[1].activeColumn == 0);
    Clip* clip = r.comp.activeClip(1);
    CHECK(clip != nullptr);
    if (clip) {
        CHECK(closeTo(clip->speed, 1.0));
        r.fake.set(1, 3, 255);
        r.apply();
        CHECK(closeTo(clip->speed, 4.0));
        r.fake.set(1, 3, 0);
        r.apply();
        CHECK(closeTo(clip->speed, 0.0));
        r.fake.set(1, 3, 192);
        r.apply();
        CHECK(closeTo(clip->speed, 1.0 + 3.0 * 64.0 / 127.0, 0.01));
    }
    r.fake.set(1, 2, 30);
    r.apply();
    r.apply();
    Clip* second = r.comp.activeClip(1);
    CHECK(second && closeTo(second->speed, 1.0 + 3.0 * 64.0 / 127.0, 0.01));

    r.fake.set(1, 4, 28 * 3 + 5);
    r.apply();
    CHECK(r.comp.layers[1].blendMode == BlendMode::Multiply);
    r.fake.set(1, 4, 255);
    r.apply();
    CHECK(r.comp.layers[1].blendMode == BlendMode::Difference);
    r.fake.set(1, 4, 0);
    r.apply();
    CHECK(r.comp.layers[1].blendMode == BlendMode::Normal);

    r.fake.set(1, 5, 255);
    r.fake.set(1, 6, 255);
    r.apply();
    const std::vector<media::ParamDesc> blurDescs = r.comp.layers[1].effects.items[0].paramDescs();
    const media::ParamSet set(blurDescs, r.comp.layers[1].effects.items[0].params);
    CHECK(closeTo(set.f("radius"), 200.0, 0.01));
    CHECK(set.s("quality") == "high");
    r.fake.set(1, 5, 128);
    r.fake.set(1, 6, 0);
    r.apply();
    const media::ParamSet set2(blurDescs, r.comp.layers[1].effects.items[0].params);
    CHECK(closeTo(set2.f("radius"), 200.0 * 128.0 / 255.0, 0.01));
    CHECK(set2.s("quality") == "low");
}

void testMasterBlock()
{
    section("fixture Master: opacidade, blackout e coluna");
    Rig r;
    r.masterBlock(2, 10);
    r.fake.set(2, 10, 255);
    r.fake.set(2, 11, 0);
    r.fake.set(2, 12, 0);
    r.apply();
    CHECK(closeTo(r.comp.masterOpacity, 1.0) && !r.comp.blackout);
    r.fake.set(2, 10, 102);
    r.apply();
    CHECK(closeTo(r.comp.masterOpacity, 0.4));
    r.fake.set(2, 11, 127);
    r.apply();
    CHECK(!r.comp.blackout);
    r.fake.set(2, 11, 128);
    r.apply();
    CHECK(r.comp.blackout);
    r.fake.set(2, 11, 0);
    r.apply();
    CHECK(!r.comp.blackout);
    r.fake.set(2, 12, 22);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == 0 && r.comp.layers[1].activeColumn == 0 && r.comp.layers[2].activeColumn == 0);
    r.fake.set(2, 12, 15);
    r.apply();
    CHECK(r.comp.layers[0].activeColumn == -1 && r.comp.layers[2].activeColumn == -1);
}

void testReleaseDefaults()
{
    section("sinal perdido: soltar volta ao valor neutro");
    Rig r;
    r.layerBlock(0, LayerMode::Full);
    r.masterBlock(1, 9);
    r.fake.set(1, 1, 200);
    r.fake.set(1, 3, 255);
    r.fake.set(1, 4, 100);
    r.fake.set(1, 5, 255);
    r.fake.set(1, 7, 255);
    r.fake.set(1, 9, 200);
    r.fake.set(1, 10, 255);
    r.apply();
    CHECK(closeTo(r.comp.layers[0].opacity, 200.0 / 255.0));
    CHECK(r.comp.layers[0].blendMode != BlendMode::Normal);
    r.fake.release(1, true);
    r.apply();
    CHECK(closeTo(r.comp.layers[0].opacity, 0.0));
    CHECK(r.comp.layers[0].blendMode == BlendMode::Normal);
    CHECK(r.comp.layers[0].transform.isIdentity(64, 64) || closeTo(r.comp.layers[0].transform.x, 0.0, 0.01));
    CHECK(closeTo(r.comp.masterOpacity, 1.0));
    CHECK(r.comp.layers[0].activeColumn == -1);
    Rig h;
    h.layerBlock(0, LayerMode::Standard);
    h.fake.set(1, 1, 200);
    h.apply();
    UniverseView& uv = h.fake.uv(1);
    uv.active = false;
    uv.lost = true;
    uv.released = false;
    h.apply();
    CHECK(closeTo(h.comp.layers[0].opacity, 200.0 / 255.0));
}

void testLayerIds()
{
    section("camadas pelo Layer::id estavel");
    Rig r;
    r.layerBlock(0, LayerMode::Basic, 1, 1);
    const int firstId = r.layerId(0);
    r.fake.set(1, 1, 255);
    r.apply();
    CHECK(closeTo(r.comp.layers[0].opacity, 1.0));
    std::swap(r.comp.layers[0], r.comp.layers[2]);
    r.comp.layers[2].opacity = 0.0f;
    CHECK(r.comp.findLayerById(firstId) == 2);
    r.fake.set(1, 1, 128);
    r.apply();
    CHECK(closeTo(r.comp.layers[2].opacity, 128.0 / 255.0));
    CHECK(closeTo(r.comp.layers[0].opacity, 1.0));
    r.comp.layers.erase(r.comp.layers.begin() + 2);
    r.fake.set(1, 1, 10);
    CHECK(r.apply() == 0);
    const int nextBefore = r.comp.nextLayerId;
    r.comp.addLayer();
    CHECK(r.comp.layers.back().id == nextBefore && r.comp.layers.back().id != firstId);
}

void testChannelsOverlap()
{
    section("uso dos canais");
    Rig r;
    r.layerBlock(0, LayerMode::Standard, 1, 1);
    r.masterBlock(1, 5);
    Map m;
    m.type = Map::Type::Param;
    m.param = ParamKind::LayerOpacity;
    m.layerId = r.layerId(1);
    m.universe = 1;
    m.channel = 20;
    r.comp.dmx.addMap(m);
    const std::vector<std::string> owners = dmx::channelOwnersFor(r.comp, 1);
    CHECK(owners.size() == 512);
    CHECK(owners[0].find("Opacidade") != std::string::npos);
    CHECK(owners[3].find("Blend") != std::string::npos);
    CHECK(owners[4].find("Master") != std::string::npos);
    CHECK(owners[19].find("Aprendido") != std::string::npos);
    CHECK(owners[10].empty());
    CHECK(r.comp.dmx.universesUsed() == std::vector<int>{1});
}

Map actionMap(const std::string& action, const control::ActionArgs& args, control::Mode mode, int universe, int channel, int threshold = 128)
{
    Map m;
    m.type = Map::Type::Action;
    m.universe = universe;
    m.channel = channel;
    m.binding.action = action;
    m.binding.args = args;
    m.binding.mode = mode;
    m.threshold = threshold;
    return m;
}

void testThresholdActions()
{
    section("canal aprendido: acao por limiar (modo segurar)");
    Rig r;
    control::ActionArgs a;
    a.layerId = r.layerId(1);
    r.comp.dmx.addMap(actionMap("layer.power", a, control::Mode::Hold, 1, 10));
    r.fake.set(1, 10, 0);
    r.apply();
    CHECK(!r.comp.layers[1].bypass);
    r.fake.set(1, 10, 200);
    r.apply();
    CHECK(r.comp.layers[1].bypass);
    CHECK(r.keys.dispatcher.heldCount() == 1 && r.keys.dispatcher.tokenHeld(r.comp.dmx.maps[0].uid));
    r.fake.set(1, 10, 125);
    r.apply();
    CHECK(r.comp.layers[1].bypass && r.keys.dispatcher.heldCount() == 1);
    r.fake.set(1, 10, 100);
    r.apply();
    CHECK(!r.comp.layers[1].bypass && r.keys.dispatcher.heldCount() == 0);
    r.fake.set(1, 10, 255);
    r.apply();
    CHECK(r.comp.layers[1].bypass);
    {
        control::ActionContext ctx = r.keys.context(r.comp, r.sel);
        const auto released = r.keys.dispatcher.releaseUp([](const control::KeyBinding&) { return false; }, ctx);
        CHECK(released.empty() && r.comp.layers[1].bypass);
    }
    r.fake.set(1, 10, 0);
    r.apply();
    CHECK(!r.comp.layers[1].bypass);

    section("canal aprendido: acao por limiar (pressionar)");
    Rig p;
    control::ActionArgs c;
    c.layerId = p.layerId(1);
    c.column = 1;
    p.comp.dmx.addMap(actionMap("clip.trigger", c, control::Mode::Press, 2, 3, 100));
    p.fake.set(2, 3, 0);
    p.apply();
    p.fake.set(2, 3, 99);
    p.apply();
    CHECK(p.comp.layers[1].activeColumn == -1);
    p.fake.set(2, 3, 100);
    p.apply();
    CHECK(p.comp.layers[1].activeColumn == 1);
    p.comp.stopLayer(1);
    p.fake.set(2, 3, 255);
    p.apply();
    CHECK(p.comp.layers[1].activeColumn == -1);
    p.fake.set(2, 3, 0);
    p.apply();
    p.fake.set(2, 3, 130);
    p.apply();
    CHECK(p.comp.layers[1].activeColumn == 1);
    CHECK(p.keys.info().firedCount >= 2);

    section("canal aprendido: parametro, invertido e 16 bits");
    Rig q;
    Map op;
    op.type = Map::Type::Param;
    op.param = ParamKind::LayerOpacity;
    op.layerId = q.layerId(0);
    op.universe = 1;
    op.channel = 5;
    q.comp.dmx.addMap(op);
    Map inv = op;
    inv.layerId = q.layerId(1);
    inv.channel = 6;
    inv.invert = true;
    q.comp.dmx.addMap(inv);
    Map wide = op;
    wide.layerId = q.layerId(2);
    wide.channel = 7;
    wide.bit16 = true;
    q.comp.dmx.addMap(wide);
    q.fake.set(1, 5, 0);
    q.fake.set(1, 6, 0);
    q.fake.set16(1, 7, 0);
    q.comp.layers[0].opacity = 0.7f;
    q.comp.layers[1].opacity = 0.7f;
    q.comp.layers[2].opacity = 0.7f;
    q.apply();
    CHECK(closeTo(q.comp.layers[0].opacity, 0.7));
    q.fake.set(1, 5, 255);
    q.fake.set(1, 6, 255);
    q.fake.set16(1, 7, 0x8000);
    q.apply();
    CHECK(closeTo(q.comp.layers[0].opacity, 1.0));
    CHECK(closeTo(q.comp.layers[1].opacity, 0.0));
    CHECK(closeTo(q.comp.layers[2].opacity, 32768.0 / 65535.0, 1.0e-4));
    q.fake.release(1, true);
    q.comp.layers[0].opacity = 0.33f;
    q.apply();
    CHECK(closeTo(q.comp.layers[0].opacity, 0.33));
}

void testFxEvents()
{
    section("canal aprendido: eventos de efeito (borda de subida e segurar)");
    Rig r;
    r.comp.layers[0].effects.add("bsl:disparo");
    r.comp.layers[0].effects.add("bsl:borda-neon");
    auto counter = [&](int effect, const char* id) {
        const media::Json* v = r.comp.layers[0].effects.items[static_cast<size_t>(effect)].params.find(id);
        return (v && v->is_number_integer()) ? v->get<int>() : 0;
    };
    auto rajada = [&] {
        const media::Json* v = r.comp.layers[0].effects.items[0].params.find("rajada");
        return v && v->is_boolean() && v->get<bool>();
    };
    control::ActionArgs next;
    next.fxScope = 1;
    next.layerId = r.layerId(0);
    next.effect = 0;
    next.param = "proximo";
    control::ActionArgs burst = next;
    burst.param = "rajada";
    control::ActionArgs flash = next;
    flash.effect = 1;
    flash.param = "flash";
    r.comp.dmx.addMap(actionMap("fx.event", next, control::Mode::Press, 1, 20));
    r.comp.dmx.addMap(actionMap("fx.event", burst, control::Mode::Hold, 1, 21));
    r.comp.dmx.addMap(actionMap("fx.event", flash, control::Mode::Press, 1, 22));
    r.comp.dmx.addMap(actionMap("fx.event", {}, control::Mode::Press, 1, 23));
    for (int ch : {20, 21, 22, 23})
        r.fake.set(1, ch, 0);
    r.apply();
    CHECK(counter(0, "proximo") == 0 && !rajada());

    r.fake.set(1, 20, 127);
    r.apply();
    CHECK(counter(0, "proximo") == 0);
    r.fake.set(1, 20, 128);
    r.apply();
    CHECK(counter(0, "proximo") == 1);
    r.fake.set(1, 20, 255);
    r.apply();
    CHECK(counter(0, "proximo") == 1);
    r.fake.set(1, 20, 0);
    r.apply();
    CHECK(counter(0, "proximo") == 1);
    r.fake.set(1, 20, 200);
    r.apply();
    CHECK(counter(0, "proximo") == 2);

    r.fake.set(1, 22, 255);
    r.apply();
    CHECK(counter(1, "flash") == 1);

    r.fake.set(1, 21, 255);
    r.apply();
    CHECK(rajada() && r.keys.dispatcher.heldCount() == 1);
    r.fake.set(1, 21, 130);
    r.apply();
    CHECK(rajada());
    r.fake.set(1, 21, 100);
    r.apply();
    CHECK(!rajada() && r.keys.dispatcher.heldCount() == 0);

    r.fake.set(1, 23, 255);
    r.apply();
    CHECK(counter(0, "proximo") == 2);

    Rig removed;
    removed.comp.layers[0].effects.add("bsl:disparo");
    removed.comp.dmx.addMap(actionMap("fx.event", burst, control::Mode::Hold, 1, 30));
    removed.comp.dmx.maps[0].binding.args.layerId = removed.layerId(0);
    removed.fake.set(1, 30, 0);
    removed.apply();
    removed.fake.set(1, 30, 255);
    removed.apply();
    CHECK(removed.keys.dispatcher.heldCount() == 1);
    removed.comp.dmx.maps.clear();
    removed.apply();
    CHECK(removed.keys.dispatcher.heldCount() == 0);

    nlohmann::json j = configToJson(r.comp.dmx);
    Config back = configFromJson(j);
    CHECK(back.maps.size() == 4 && back.maps[1].binding.args.param == "rajada" && back.maps[1].binding.mode == control::Mode::Hold
          && back.maps[0].binding.args.param == "proximo" && back.maps[0].threshold == 128);

    section("perfil exportado nao muda com mapas de evento");
    Rig p;
    p.layerBlock(0, LayerMode::Standard, 1, 1);
    p.layerBlock(1, LayerMode::Full, 1, 40);
    p.masterBlock(1, 100);
    auto namer = [](int id) { return "Camada " + std::to_string(id); };
    const auto before = fixtureTypes(p.comp.dmx);
    const std::string csv0 = csvChannels(before), patch0 = csvPatch(p.comp.dmx, namer), html0 = htmlChannelTable(before, p.comp.dmx, namer);
    std::vector<std::string> gdtf0;
    for (const FixtureType& t : before)
        gdtf0.push_back(gdtfDescription(t, "2025-01-01"));
    const std::vector<ChannelRow> rows0 = layerRows(LayerMode::Full, 2);
    control::ActionArgs a = next;
    a.layerId = p.layerId(0);
    p.comp.dmx.addMap(actionMap("fx.event", a, control::Mode::Press, 1, 200));
    a.param = "rajada";
    p.comp.dmx.addMap(actionMap("fx.event", a, control::Mode::Hold, 1, 201));
    const auto after = fixtureTypes(p.comp.dmx);
    CHECK(csvChannels(after) == csv0 && csvPatch(p.comp.dmx, namer).size() > 0 && html0 == htmlChannelTable(after, p.comp.dmx, namer));
    CHECK(after.size() == before.size());
    bool sameGdtf = after.size() == gdtf0.size();
    for (size_t i = 0; sameGdtf && i < after.size(); ++i)
        sameGdtf = gdtfDescription(after[i], "2025-01-01") == gdtf0[i];
    CHECK(sameGdtf);
    const std::vector<ChannelRow> rows1 = layerRows(LayerMode::Full, 2);
    bool sameRows = rows0.size() == rows1.size();
    for (size_t i = 0; sameRows && i < rows0.size(); ++i)
        sameRows = rows0[i].name == rows1[i].name && rows0[i].range == rows1[i].range && rows0[i].offset == rows1[i].offset;
    CHECK(sameRows);
    CHECK(patch0.find("Master") != std::string::npos);
}

void testLearn()
{
    section("DMX learn");
    Rig r;
    r.fake.uv(1);
    r.fake.uv(2);
    r.fake.set(1, 3, 40);
    LearnTarget target;
    target.prototype.type = Map::Type::Param;
    target.prototype.param = ParamKind::LayerOpacity;
    target.prototype.layerId = r.layerId(1);
    target.title = "Opacidade da camada 2";
    r.comp.layers[1].opacity = 0.9f;
    r.engine.learnBegin(target, r.fake.snap);
    CHECK(r.engine.learning());
    r.fake.set(1, 3, 42);
    r.apply();
    CHECK(r.engine.learning() && r.comp.dmx.maps.empty());
    r.fake.set(2, 9, 100);
    r.apply();
    CHECK(!r.engine.learning() && r.comp.dmx.maps.size() == 1);
    CHECK(r.comp.dmx.maps[0].universe == 2 && r.comp.dmx.maps[0].channel == 9);
    CHECK(r.comp.dmx.maps[0].layerId == r.layerId(1) && r.comp.dmx.maps[0].param == ParamKind::LayerOpacity);
    CHECK(closeTo(r.comp.layers[1].opacity, 0.9));
    std::string msg;
    CHECK(r.engine.takeLearned(&msg) > 0 && msg.find("U2.9") != std::string::npos);
    CHECK(r.engine.takeLearned() == 0);
    r.fake.set(2, 9, 255);
    r.apply();
    CHECK(closeTo(r.comp.layers[1].opacity, 1.0));
    r.engine.learnBegin(target, r.fake.snap);
    r.fake.set(1, 20, 250);
    r.apply();
    CHECK(r.comp.dmx.maps.size() == 1 && r.comp.dmx.maps[0].universe == 1 && r.comp.dmx.maps[0].channel == 20);
    LearnTarget other = target;
    other.prototype.layerId = r.layerId(2);
    r.engine.learnBegin(other, r.fake.snap);
    r.fake.set(1, 20, 5);
    r.apply();
    CHECK(r.comp.dmx.maps.size() == 1 && r.comp.dmx.maps[0].layerId == r.layerId(2));
    r.engine.takeLearned();

    LearnTarget act;
    act.prototype.type = Map::Type::Action;
    act.prototype.binding.action = "master.blackout";
    act.prototype.binding.mode = control::Mode::Hold;
    act.title = "Blackout";
    r.engine.learnBegin(act, r.fake.snap);
    r.fake.set(2, 1, 77);
    r.apply();
    CHECK(r.comp.dmx.maps.size() == 2);
    r.engine.takeLearned();
    r.fake.set(2, 1, 0);
    r.apply();
    CHECK(!r.comp.blackout);
    r.fake.set(2, 1, 200);
    r.apply();
    CHECK(r.comp.blackout);
    r.fake.set(2, 1, 0);
    r.apply();
    CHECK(!r.comp.blackout);
    r.engine.learnBegin(act, r.fake.snap);
    r.engine.learnCancel();
    CHECK(!r.engine.learning());
}

void testProject()
{
    section("projeto: ida e volta e migracao");
    Composition comp = makeComp();
    comp.layers[0].effects.add("native:blur");
    {
        Block a;
        a.layerId = comp.layers[0].id;
        a.mode = LayerMode::Full16;
        a.universe = 2;
        a.channel = 17;
        FxSlot s;
        s.effect = 0;
        s.param = "radius";
        a.fx.push_back(s);
        comp.dmx.addBlock(a);
        Block b;
        b.layerId = comp.layers[2].id;
        b.mode = LayerMode::Basic;
        b.enabled = false;
        comp.dmx.addBlock(b);
        Block m;
        m.kind = BlockKind::Master;
        m.universe = 3;
        m.channel = 100;
        comp.dmx.addBlock(m);
        Map p;
        p.type = Map::Type::Param;
        p.param = ParamKind::EffectParam;
        p.fxScope = 0;
        p.layerId = comp.layers[0].id;
        p.deck = 0;
        p.column = 1;
        p.effect = 0;
        p.paramId = "radius";
        p.universe = 4;
        p.channel = 5;
        p.bit16 = true;
        p.invert = true;
        comp.dmx.addMap(p);
        control::ActionArgs args;
        args.layerId = comp.layers[1].id;
        args.column = 2;
        comp.dmx.addMap(actionMap("clip.trigger", args, control::Mode::Press, 5, 6, 77));
        control::ActionArgs hold;
        hold.layerId = comp.layers[1].id;
        comp.dmx.addMap(actionMap("layer.solo", hold, control::Mode::Hold, 5, 7));
    }
    const OutputSetup output = OutputSetup::createDefault(comp.width, comp.height, {});
    const std::string text = project::serialize(comp, output, {});
    CHECK(text.find("\"dmx\"") != std::string::npos);
    Composition back;
    OutputSetup outBack;
    const project::LoadResult res = project::parse(text, {}, back, outBack);
    CHECK(res.ok);
    CHECK(back.dmx.sameContent(comp.dmx));
    CHECK(back.dmx.blocks.size() == 3 && back.dmx.maps.size() == 3);
    CHECK(back.dmx.blocks[0].layerId == back.layers[0].id && back.dmx.blocks[0].fx.size() == 1);
    CHECK(!back.dmx.blocks[1].enabled);
    CHECK(back.dmx.maps[0].bit16 && back.dmx.maps[0].invert && back.dmx.maps[0].fxScope == 0 && back.dmx.maps[0].column == 1);
    CHECK(back.dmx.maps[2].binding.mode == control::Mode::Hold && back.dmx.maps[1].threshold == 77);
    CHECK(back.dmx.maps[1].binding.args.layerId == back.layers[1].id && back.dmx.maps[1].binding.args.column == 2);
    for (const Block& b : back.dmx.blocks)
        CHECK(b.uid > 0);
    CHECK(project::serialize(back, outBack, {}) == text || back.dmx.sameContent(comp.dmx));

    Composition plain = makeComp();
    const std::string plainText = project::serialize(plain, output, {});
    CHECK(plainText.find("\"dmx\"") == std::string::npos);
    Composition plainBack;
    OutputSetup plainOut;
    CHECK(project::parse(plainText, {}, plainBack, plainOut).ok && plainBack.dmx.empty());

    nlohmann::json j = nlohmann::json::parse(text);
    j.erase("dmx");
    Composition oldBack;
    OutputSetup oldOut;
    CHECK(project::parse(j.dump(), {}, oldBack, oldOut).ok && oldBack.dmx.empty());

    nlohmann::json legacy = nlohmann::json::parse(text);
    legacy["dmx"] = {{"version", 1},
                     {"blocks", nlohmann::json::array({{{"kind", "layer"}, {"layer", 1}, {"mode", "standard"}, {"universe", 1}, {"channel", 1}}})},
                     {"maps", nlohmann::json::array({
                          {{"type", "param"}, {"param", "layerOpacity"}, {"layer", 2}, {"universe", 1}, {"channel", 9}},
                          {{"type", "action"}, {"threshold", 90}, {"universe", 1}, {"channel", 10},
                           {"binding", {{"action", "layer.clear"}, {"args", {{"layer", 2}}}}}},
                          {{"type", "action"}, {"universe", 1}, {"channel", 11}, {"binding", {{"action", "acao.que.nao.existe"}}}},
                          {{"type", "action"}, {"universe", 1}, {"channel", 12}, {"binding", {{"action", "project.save"}}}},
                      })}};
    Composition mig;
    OutputSetup migOut;
    const project::LoadResult migRes = project::parse(legacy.dump(), {}, mig, migOut);
    CHECK(migRes.ok);
    CHECK(mig.dmx.blocks.size() == 1 && mig.dmx.blocks[0].layerId == mig.layers[1].id);
    CHECK(mig.dmx.maps.size() == 2);
    if (mig.dmx.maps.size() == 2) {
        CHECK(mig.dmx.maps[0].layerId == mig.layers[2].id);
        CHECK(mig.dmx.maps[1].binding.args.layerId == mig.layers[2].id && mig.dmx.maps[1].binding.args.layer == 0);
    }
    CHECK(migRes.warnings.size() >= 2);
    nlohmann::json junk = nlohmann::json::parse(text);
    junk["dmx"] = {{"blocks", nlohmann::json::array({1, "x", {{"universe", 99999999}, {"channel", -4}, {"mode", "??"}}})},
                   {"maps", nlohmann::json::array({nullptr, {{"param", "naoExiste"}}})}};
    Composition ju;
    OutputSetup juOut;
    CHECK(project::parse(junk.dump(), {}, ju, juOut).ok);
    CHECK(ju.dmx.blocks.size() == 1 && ju.dmx.blocks[0].universe <= kMaxUniverse && ju.dmx.blocks[0].channel >= 1 && ju.dmx.maps.empty());
    nlohmann::json future = nlohmann::json::parse(text);
    future["dmx"]["version"] = 99;
    Composition fu;
    OutputSetup fuOut;
    const project::LoadResult fr = project::parse(future.dump(), {}, fu, fuOut);
    CHECK(fr.ok && fu.dmx.blocks.size() == 3 && !fr.warnings.empty());
}

struct UdpSender
{
    bsl::sock::Handle s = bsl::sock::kInvalid;
    sockaddr_in dest{};
    UdpSender()
    {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in me{};
        me.sin_family = AF_INET;
        me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(s, reinterpret_cast<sockaddr*>(&me), sizeof(me));
        bsl::sock::setTimeoutsMs(s, 1500, 1500);
    }
    ~UdpSender()
    {
        if (s != bsl::sock::kInvalid)
            bsl::sock::close(s);
    }
    void to(const char* ip, std::uint16_t port)
    {
        dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        inet_pton(AF_INET, ip, &dest.sin_addr);
    }
    void send(const std::vector<std::uint8_t>& bytes)
    {
        sendto(s, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&dest),
               sizeof(dest));
    }
    std::vector<std::uint8_t> receive()
    {
        std::vector<std::uint8_t> buf(2048);
        const int n = recv(s, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
        buf.resize(n > 0 ? static_cast<size_t>(n) : 0);
        return buf;
    }
};

template <class F>
bool waitFor(Receiver& r, Snapshot& snap, double seconds, F cond)
{
    const double end = Receiver::clock() + seconds;
    while (Receiver::clock() < end) {
        r.poll(snap);
        if (cond(snap))
            return true;
        sleepMs(8);
    }
    r.poll(snap);
    return cond(snap);
}

std::array<std::uint8_t, 16> cidOf(std::uint8_t b)
{
    std::array<std::uint8_t, 16> c{};
    c.fill(b);
    return c;
}

bool startLoopback(Receiver& r, NetConfig cfg, std::uint16_t& artPort, std::uint16_t& sacnPort)
{
    cfg.enabled = true;
    cfg.interfaceIp = "127.0.0.1";
    std::string error;
    r.setPorts(kArtNetPort, kSacnPort);
    if (r.start(cfg, {}, &error)) {
        artPort = kArtNetPort;
        sacnPort = kSacnPort;
        return true;
    }
    std::printf("  AVISO: portas padrao ocupadas (%s); usando 16454/15568\n", error.c_str());
    r.setPorts(16454, 15568);
    if (r.start(cfg, {}, &error)) {
        artPort = 16454;
        sacnPort = 15568;
        return true;
    }
    std::printf("  NAO TESTADO: o bind no loopback falhou (%s)\n", error.c_str());
    ++g_notTested;
    return false;
}

void testNetwork()
{
    section("receptor Art-Net / sACN por loopback UDP");
    NetConfig cfg;
    cfg.universesText = "1-3";
    cfg.timeoutSec = 0.6;
    cfg.merge = MergeMode::Htp;
    cfg.loss = LossPolicy::Hold;
    Receiver rx;
    std::uint16_t artPort = 0, sacnPort = 0;
    if (!startLoopback(rx, cfg, artPort, sacnPort))
        return;
    CHECK(rx.running());
    UdpSender tx;
    Snapshot snap;
    CHECK(rx.poll(snap) && snap.running && snap.universes.size() == 3);
    CHECK(snap.artNetOk && snap.sacnOk);
    CHECK(snap.status.find("127.0.0.1") != std::string::npos);

    std::vector<std::uint8_t> d(512, 0);
    d[0] = 200;
    d[9] = 33;
    d[511] = 99;
    tx.to("127.0.0.1", artPort);
    tx.send(buildArtDmx(0, 1, d.data(), 512));
    CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->active && u->data[0] == 200; }));
    const UniverseView* u1 = snap.find(1);
    CHECK(u1 && u1->data[9] == 33 && u1->data[511] == 99 && u1->packets >= 1);
    CHECK(u1 && !u1->sources.empty() && u1->sources[0].protocol == Protocol::ArtNet && u1->sources[0].ip == 0x7F000001u);
    d[0] = 5;
    tx.send(buildArtDmx(2, 2, d.data(), 512));
    CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(3); return u && u->active && u->data[0] == 5; }));
    tx.send(buildArtDmx(8, 3, d.data(), 512));
    sleepMs(120);
    rx.poll(snap);
    CHECK(snap.find(9) == nullptr && snap.universes.size() == 3);

    tx.to("127.0.0.1", sacnPort);
    std::vector<std::uint8_t> s2(512, 0);
    s2[0] = 111;
    s2[100] = 222;
    tx.send(buildSacnData(2, cidOf(0x11), "Software de teste", 100, 1, s2.data(), 512));
    CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(2); return u && u->active && u->data[100] == 222; }));
    const UniverseView* u2 = snap.find(2);
    CHECK(u2 && !u2->sources.empty() && u2->sources[0].protocol == Protocol::Sacn && u2->sources[0].name == "Software de teste");
    CHECK(u2 && u2->sources[0].priority == 100);

    std::vector<std::uint8_t> a(512, 0), b(512, 0);
    a[0] = 10;
    a[1] = 250;
    b[0] = 90;
    b[1] = 20;
    tx.to("127.0.0.1", artPort);
    tx.send(buildArtDmx(0, 4, a.data(), 512));
    tx.to("127.0.0.1", sacnPort);
    tx.send(buildSacnData(1, cidOf(0x22), "Mesa de teste", 100, 1, b.data(), 512));
    CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->data[0] == 90 && u->data[1] == 250; }));
    const UniverseView* merged = snap.find(1);
    CHECK(merged && merged->sources.size() == 2);
    std::vector<std::uint8_t> top(512, 0);
    top[0] = 7;
    tx.send(buildSacnData(1, cidOf(0x22), "Mesa de teste", 150, 2, top.data(), 512));
    CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->data[0] == 7 && u->data[1] == 0; }));

    {
        UdpSender poller;
        poller.to("127.0.0.1", artPort);
        poller.send(buildArtPoll());
        const std::vector<std::uint8_t> bytes = poller.receive();
        ArtPollReplyView rv;
        CHECK(!bytes.empty() && parseArtPollReply(bytes.data(), bytes.size(), rv));
        CHECK(rv.shortName == "BSL Stage" && rv.longName.find("BSL Stage") != std::string::npos);
        CHECK(rv.numPorts == 3 && rv.swOut[0] == 0 && rv.swOut[1] == 1 && rv.swOut[2] == 2);
        CHECK(rv.portTypes[0] == 0x80 && rv.net == 0 && rv.sub == 0 && rv.bindIndex == 1);
        CHECK(rv.ip[0] == 127 && rv.ip[3] == 1);
        CHECK(waitFor(rx, snap, 2.0, [](const Snapshot& sn) { return sn.artPolls >= 1 && sn.artReplies >= 1; }));
    }

    {
        tx.to("127.0.0.1", artPort);
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1400);
        std::uint8_t seq = 10;
        while (std::chrono::steady_clock::now() < end) {
            d[0] = static_cast<std::uint8_t>(seq);
            tx.send(buildArtDmx(2, ++seq, d.data(), 512));
            sleepMs(25);
        }
        rx.poll(snap);
        const UniverseView* u3 = snap.find(3);
        CHECK(u3 && u3->packetsPerSec >= 15.0f && u3->packetsPerSec <= 60.0f);
        CHECK(snap.totalPackets > 40);
    }

    {
        tx.to("127.0.0.1", artPort);
        double worst = 0.0;
        for (int i = 0; i < 400; ++i)
            tx.send(buildArtDmx(0, static_cast<std::uint8_t>(i + 1), a.data(), 512));
        for (int i = 0; i < 300; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            rx.poll(snap);
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::printf("  pior poll() = %.3f ms\n", worst);
        CHECK(worst < 5.0);
    }

    sleepMs(1000);
    rx.poll(snap);
    {
        const UniverseView* u3 = snap.find(3);
        CHECK(u3 && !u3->active && u3->lost && !u3->released);
        CHECK(u3 && u3->data[1] == 0 && u3->data[9] == 33);
        const UniverseView* u2b = snap.find(2);
        CHECK(u2b && u2b->lost && u2b->data[100] == 222);
    }
    rx.stop();
    CHECK(!rx.running());

    section("receptor: soltar ao perder o sinal");
    {
        NetConfig rel = cfg;
        rel.loss = LossPolicy::Release;
        Receiver rr;
        std::uint16_t ap = 0, sp = 0;
        if (startLoopback(rr, rel, ap, sp)) {
            UdpSender t2;
            t2.to("127.0.0.1", ap);
            t2.send(buildArtDmx(0, 1, d.data(), 512));
            Snapshot s2snap;
            CHECK(waitFor(rr, s2snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->active; }));
            CHECK(waitFor(rr, s2snap, 3.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->lost; }));
            const UniverseView* u = s2snap.find(1);
            CHECK(u && u->released && u->data[0] == 0 && u->data[9] == 0);
        }
    }

    section("receptor: Art-Net 1 como primeiro universo");
    {
        NetConfig shifted = cfg;
        shifted.artNetFirstUniverse = 1;
        shifted.universesText = "1-2";
        Receiver rs;
        std::uint16_t ap = 0, sp = 0;
        if (startLoopback(rs, shifted, ap, sp)) {
            UdpSender t3;
            t3.to("127.0.0.1", ap);
            t3.send(buildArtDmx(1, 1, d.data(), 512));
            Snapshot sn;
            CHECK(waitFor(rs, sn, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(1); return u && u->active; }));
            t3.send(buildArtDmx(0, 2, d.data(), 512));
            sleepMs(120);
            rs.poll(sn);
            CHECK(sn.find(2) && !sn.find(2)->everSeen);
            t3.send(buildArtPoll());
            const std::vector<std::uint8_t> bytes = t3.receive();
            ArtPollReplyView rv;
            CHECK(!bytes.empty() && parseArtPollReply(bytes.data(), bytes.size(), rv) && rv.swOut[0] == 1 && rv.swOut[1] == 2);
        }
    }
}

void testService()
{
    section("servico: rede -> motor -> composicao");
    Composition comp = makeComp();
    Block b;
    b.layerId = comp.layers[0].id;
    b.mode = LayerMode::Standard;
    b.universe = 1;
    b.channel = 1;
    comp.dmx.addBlock(b);
    control::KeyService keys;
    control::SelectionRef sel;
    Service svc;
    NetConfig cfg;
    cfg.enabled = true;
    cfg.interfaceIp = "127.0.0.1";
    cfg.universesText = "1";
    cfg.timeoutSec = 0.6;
    {
        Receiver& rx = svc.receiver();
        rx.setPorts(16456, 15570);
    }
    CHECK(svc.state() == Service::State::Off);
    svc.update(cfg, comp, keys, sel);
    if (!svc.receiver().running()) {
        std::printf("  NAO TESTADO: o servico nao subiu o receptor (%s)\n", svc.error().c_str());
        ++g_notTested;
        return;
    }
    CHECK(svc.state() == Service::State::Waiting);
    UdpSender tx;
    tx.to("127.0.0.1", 16456);
    std::vector<std::uint8_t> d(512, 0);
    d[0] = 51;
    d[1] = 0;
    tx.send(buildArtDmx(0, 1, d.data(), 512));
    const double end = Receiver::clock() + 2.0;
    while (Receiver::clock() < end && std::fabs(comp.layers[0].opacity - 0.2f) > 0.01f) {
        svc.update(cfg, comp, keys, sel);
        sleepMs(8);
    }
    CHECK(closeTo(comp.layers[0].opacity, 0.2));
    CHECK(svc.state() == Service::State::Receiving);
    d[1] = 22;
    tx.send(buildArtDmx(0, 2, d.data(), 512));
    const double end2 = Receiver::clock() + 2.0;
    while (Receiver::clock() < end2 && comp.layers[0].activeColumn != 0) {
        svc.update(cfg, comp, keys, sel);
        sleepMs(8);
    }
    CHECK(comp.layers[0].activeColumn == 0);
    const double end3 = Receiver::clock() + 3.0;
    while (Receiver::clock() < end3 && svc.state() != Service::State::Lost) {
        svc.update(cfg, comp, keys, sel);
        sleepMs(20);
    }
    CHECK(svc.state() == Service::State::Lost);
    CHECK(closeTo(comp.layers[0].opacity, 0.2));
    cfg.enabled = false;
    svc.update(cfg, comp, keys, sel);
    CHECK(svc.state() == Service::State::Off && !svc.receiver().running());
    svc.shutdown();
}

void testMulticast()
{
    section("sACN multicast (opcional)");
    const std::vector<NetInterface> ifaces = listInterfaces(false);
    if (ifaces.empty()) {
        std::printf("  NAO TESTADO: nenhuma placa de rede ativa\n");
        ++g_notTested;
        return;
    }
    const NetInterface& nic = ifaces.front();
    NetConfig cfg;
    cfg.enabled = true;
    cfg.interfaceIp = nic.ip;
    cfg.universesText = "7";
    cfg.artNet = false;
    Receiver rx;
    rx.setPorts(16457, 15571);
    std::string error;
    if (!rx.start(cfg, {}, &error)) {
        std::printf("  NAO TESTADO: %s\n", error.c_str());
        ++g_notTested;
        return;
    }
    Snapshot snap;
    rx.poll(snap);
    std::printf("  placa %s (%s): inscricoes multicast %d ok, %d falha(s)\n", nic.name.c_str(), nic.ip.c_str(), snap.multicastJoined,
                snap.multicastFailed);
    CHECK(snap.multicastJoined == 1);
    UdpSender tx;
    in_addr ifaceAddr{};
    inet_pton(AF_INET, nic.ip.c_str(), &ifaceAddr);
    setsockopt(tx.s, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&ifaceAddr), sizeof(ifaceAddr));
    std::vector<std::uint8_t> d(512, 0);
    d[0] = 77;
    tx.to(ipToString(sacnMulticastAddress(7)).c_str(), 15571);
    for (int i = 0; i < 5; ++i) {
        tx.send(buildSacnData(7, cidOf(0x33), "multicast", 100, static_cast<std::uint8_t>(i + 1), d.data(), 512));
        sleepMs(20);
    }
    const bool got = waitFor(rx, snap, 2.0, [](const Snapshot& s) { const UniverseView* u = s.find(7); return u && u->active && u->data[0] == 77; });
    CHECK(got);
}

size_t countOf(const std::string& text, const std::string& needle)
{
    size_t n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
        ++n;
    return n;
}

std::vector<std::string> csvFields(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ';') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

void testExport()
{
    section("XML: verificador proprio");
    std::string err;
    CHECK(xmlWellFormed("<?xml version=\"1.0\"?><a x=\"1\"><b/><c y='&amp;'>t&lt;</c></a>", &err));
    CHECK(!xmlWellFormed("<a><b></a></b>", &err));
    CHECK(!xmlWellFormed("<a x=1/>", &err));
    CHECK(!xmlWellFormed("<a x=\"1\" x=\"2\"/>", &err));
    CHECK(!xmlWellFormed("<a>&bogus;</a>", &err));
    CHECK(!xmlWellFormed("<a/><b/>", &err));
    CHECK(!xmlWellFormed("<a>", &err));
    CHECK(!xmlWellFormed("texto", &err));

    section("exportacao: GDTF, CSV e HTML");
    Composition comp = makeComp();
    comp.layers[1].name = "Fundo & \"Palco\" <1>";
    {
        Block a;
        a.layerId = comp.layers[0].id;
        a.mode = LayerMode::Full16;
        a.universe = 1;
        a.channel = 1;
        FxSlot s1, s2, s3, s4;
        a.fx = {s1, s2, s3, s4};
        comp.dmx.addBlock(a);
        Block b;
        b.layerId = comp.layers[1].id;
        b.mode = LayerMode::Standard;
        b.universe = 1;
        b.channel = 17;
        comp.dmx.addBlock(b);
        Block m;
        m.kind = BlockKind::Master;
        m.universe = 1;
        m.channel = 30;
        comp.dmx.addBlock(m);
    }
    const std::function<std::string(int)> namer = [&comp](int id) { return dmx::layerLabel(comp, id); };
    const std::vector<FixtureType> types = fixtureTypes(comp.dmx);
    CHECK(types.size() == 2 && types[0].name == "BSL Stage Layer" && types[1].name == "BSL Stage Master");
    CHECK(types[0].modes.size() == 5);
    bool hasFx4 = false;
    for (const FixtureMode& m : types[0].modes)
        hasFx4 = hasFx4 || m.name == "Full 16bit FX4";
    CHECK(hasFx4);

    size_t totalChannels = 0;
    for (const FixtureType& t : types) {
        const std::string xml = gdtfDescription(t, "2026-11-04T12:00:00");
        std::string error;
        const bool ok = xmlWellFormed(xml, &error);
        if (!ok)
            std::printf("  XML invalido (%s): %s\n", t.name.c_str(), error.c_str());
        CHECK(ok);
        CHECK(xml.find("<GDTF DataVersion=\"1.2\">") != std::string::npos);
        CHECK(xml.find("FixtureTypeID=\"" + t.id + "\"") != std::string::npos);
        CHECK(xml.find("Manufacturer=\"BSL Light\"") != std::string::npos);
        CHECK(countOf(xml, "<DMXMode ") == t.modes.size());
        for (const FixtureMode& m : t.modes) {
            const size_t from = xml.find("<DMXMode Name=\"" + m.name + "\"");
            CHECK(from != std::string::npos);
            const size_t to = xml.find("</DMXMode>", from);
            const std::string body = xml.substr(from, to - from);
            CHECK(countOf(body, "<DMXChannel ") == m.rows.size());
            int expect = 1;
            size_t at = 0;
            for (const ChannelRow& r : m.rows) {
                at = body.find("Offset=\"", at);
                CHECK(at != std::string::npos);
                const std::string want = r.span == 2 ? std::to_string(expect) + "," + std::to_string(expect + 1) : std::to_string(expect);
                CHECK(body.compare(at, 8 + want.size() + 1, "Offset=\"" + want + "\"") == 0);
                CHECK(r.offset == expect);
                expect += r.span;
                at += 8;
                CHECK(xml.find("<Attribute Name=\"" + r.key + "\"") != std::string::npos);
            }
            CHECK(expect - 1 == m.channels);
            totalChannels += static_cast<size_t>(m.channels);
        }
        const std::vector<std::uint8_t> zip = gdtfArchive(t, "2026-11-04T12:00:00");
        std::vector<ZipFile> files;
        std::string zerr;
        CHECK(zipRead(zip, files, &zerr));
        CHECK(files.size() == 1 && files[0].name == "description.xml");
        CHECK(files.size() == 1 && std::string(files[0].data.begin(), files[0].data.end()) == xml);
        CHECK(zip.size() > 4 && zip[0] == 'P' && zip[1] == 'K');
        std::vector<std::uint8_t> broken = zip;
        broken[60] ^= 0xFF;
        CHECK(!zipRead(broken, files, &zerr));
    }
    {
        const std::string xml = gdtfDescription(types[0], "2026-11-04T12:00:00");
        CHECK(xml.find("Offset=\"5,6\"") != std::string::npos);
        CHECK(xml.find("DMXFrom=\"0/2\" Default=\"32768/2\"") != std::string::npos);
        CHECK(xml.find("Name=\"Column 24\" DMXFrom=\"250/1\"") != std::string::npos);
        CHECK(xml.find("Name=\"Difference\" DMXFrom=\"224/1\"") != std::string::npos);
        CHECK(xml.find("BSL_FX4") != std::string::npos);
        for (const std::string& name : {"Name=\"BSL Stage Layer\"", "Attribute=\"BSL_PosX\""})
            CHECK(xml.find(name) != std::string::npos);
        const std::string master = gdtfDescription(types[1], "2026-11-04T12:00:00");
        CHECK(master.find("Default=\"255/1\"") != std::string::npos);
        CHECK(master.find("Name=\"Blackout\" DMXFrom=\"128/1\"") != std::string::npos);
    }
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u);

    const std::string csv = csvChannels(types);
    CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < csv.size();) {
        const size_t nl = csv.find('\n', pos);
        lines.push_back(csv.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    CHECK(lines.size() == 1 + totalChannels);
    bool allTen = true;
    for (const std::string& l : lines)
        allTen = allTen && csvFields(l).size() == 10;
    CHECK(allTen);
    CHECK(csv.find("Posição X (grosso)") != std::string::npos && csv.find("Posição X (fino)") != std::string::npos);
    CHECK(csv.find("BSL_PosX_Fine") != std::string::npos);
    const std::string patch = csvPatch(comp.dmx, namer);
    CHECK(countOf(patch, "\r\n") == 1 + comp.dmx.blocks.size());
    CHECK(patch.find("Full 16bit FX4") != std::string::npos && patch.find("Master") != std::string::npos);
    CHECK(patch.find("\"Camada 2 (Fundo & \"\"Palco\"\" <1>)\"") != std::string::npos);
    const std::string html = htmlChannelTable(types, comp.dmx, namer);
    CHECK(countOf(html, "<table>") == countOf(html, "</table>") && countOf(html, "<table>") >= 1 + types[0].modes.size());
    CHECK(countOf(html, "<tr>") == countOf(html, "</tr>"));
    CHECK(html.find("Fundo &amp; &quot;Palco&quot; &lt;1&gt;") != std::string::npos);
    CHECK(html.find("<meta charset=\"utf-8\">") != std::string::npos && html.find("</html>") != std::string::npos);

    const fs::path tmp = fs::temp_directory_path() / ("bslstage-dmxtest-" + std::to_string(testProcessId()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    const ExportResult res = exportProfiles(tmp, comp.dmx, namer);
    CHECK(res.ok);
    if (res.ok) {
        CHECK(res.files.size() == 7);
        for (const fs::path& f : res.files)
            CHECK(fs::exists(f) && fs::file_size(f) > 0);
        CHECK(fs::exists(res.folder / L"GDTF") && fs::exists(res.folder / L"Tabelas" / L"LEIA-ME.txt"));
        size_t gdtf = 0;
        for (const auto& e : fs::directory_iterator(res.folder / L"GDTF")) {
            ++gdtf;
            std::ifstream in(e.path(), std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::vector<ZipFile> files;
            CHECK(e.path().extension() == ".gdtf" && zipRead(bytes, files, nullptr) && files.size() == 1);
            if (files.size() == 1)
                CHECK(xmlWellFormed(std::string(files[0].data.begin(), files[0].data.end()), nullptr));
        }
        CHECK(gdtf == 2);
        CHECK(fs::exists(res.folder / L"Tabelas" / L"canais.csv") && fs::exists(res.folder / L"Tabelas" / L"patch.csv") &&
              fs::exists(res.folder / L"Tabelas" / L"canais.html"));
    }
    fs::remove_all(tmp, ec);
}

int runSender(const char* host, double seconds)
{
    UdpSender tx;
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::uint8_t> a(512, 0), b(512, 0), c(512, 0), c2(512, 0);
    std::uint8_t seq = 0;
    tx.to(host, kArtNetPort);
    tx.send(buildArtPoll());
    for (;;) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (t >= seconds)
            break;
        for (int i = 0; i < 40; ++i)
            a[static_cast<size_t>(i)] = static_cast<std::uint8_t>(127.5 + 127.5 * std::sin(t * (0.6 + 0.11 * i) + i));
        a[0] = 230;
        a[8] = 200;
        a[12] = 255;
        a[24] = 255;
        a[25] = 0;
        a[26] = 0;
        for (int i = 0; i < 24; ++i)
            b[static_cast<size_t>(i)] = static_cast<std::uint8_t>(((static_cast<int>(t * 40) + i * 10) % 256));
        b[0] = 0;
        for (int i = 0; i < 64; ++i) {
            c[static_cast<size_t>(i)] = static_cast<std::uint8_t>(i * 3);
            c2[static_cast<size_t>(i)] = static_cast<std::uint8_t>(255 - i * 3);
        }
        tx.to(host, kArtNetPort);
        tx.send(buildArtDmx(0, ++seq, a.data(), 512));
        tx.send(buildArtDmx(2, seq, c.data(), 512));
        tx.to(host, kSacnPort);
        tx.send(buildSacnData(2, cidOf(0x42), "Software", 100, seq, b.data(), 512));
        tx.send(buildSacnData(3, cidOf(0x43), "Mesa de teste", 100, seq, c2.data(), 512));
        sleepMs(25);
    }
    return 0;
}

}

int main(int argc, char** argv)
{
    bsl::sock::startup();

    bool multicast = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--multicast") == 0)
            multicast = true;
        else if (std::strcmp(argv[i], "--send") == 0 && i + 2 < argc)
            return runSender(argv[i + 1], std::atof(argv[i + 2]));
    }

    if (!glfwInit())
        return 2;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "dmxtest", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "sem contexto GL 3.3\n");
        return 2;
    }
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress))
        return 2;

    testPackets();
    testMerge();
    testProfiles();
    testFixtureOpacityAndTransform();
    testFixtureClip();
    testFixtureSpeedBlendFx();
    testMasterBlock();
    testReleaseDefaults();
    testLayerIds();
    testChannelsOverlap();
    testThresholdActions();
    testFxEvents();
    testLearn();
    testProject();
    testNetwork();
    testService();
    if (multicast)
        testMulticast();
    else
        std::printf("[sACN multicast]\n  NAO TESTADO por padrao (use --multicast; abre a porta fora do loopback e pode acionar o Firewall)\n");
    testExport();

    glfwDestroyWindow(win);
    glfwTerminate();
    bsl::sock::cleanup();
    std::printf("\n%d verificacoes, %d falha(s), %d bloco(s) nao testado(s)\n", g_checks, g_failures, g_notTested);
    std::printf(g_failures == 0 ? "DMXTEST OK\n" : "DMXTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
