#include "output/MapEdit.h"
#include "output/MapHistory.h"
#include "output/OutputSetup.h"
#include "output/Snap.h"

#include <cmath>
#include <cstdio>
#include <utility>

using namespace bsl;
using namespace bsl::snap;

namespace {

int g_failures = 0;
int g_checks = 0;

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

bool near(float a, float b, float eps = 1.0e-3f)
{
    return std::abs(a - b) <= eps;
}

Moving movingRect(const RectF& r)
{
    Moving m;
    m.xs = {r.x0, r.cx(), r.x1};
    m.ys = {r.y0, r.cy(), r.y1};
    m.points = {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}};
    m.extent = r;
    return m;
}

Options opts(float zoom)
{
    Options o;
    o.zoom = zoom;
    return o;
}

void testSnapEdge()
{
    section("ima: borda entre 2 slices, com e sem zoom");
    Targets t;
    addRect(t, {0, 0, 100, 100});

    RectF b{105, 20, 205, 120};
    Result r = snapMoving(movingRect(b), t, opts(1.0f));
    CHECK(r.snappedX && near(r.dx, -5.0f));
    CHECK(near(b.x0 + r.dx, 100.0f));
    bool guide = false;
    for (const Guide& g : r.guides)
        guide = guide || (g.vertical && near(g.pos, 100.0f));
    CHECK(guide);

    b = {112, 20, 212, 120};
    r = snapMoving(movingRect(b), t, opts(0.5f));
    CHECK(r.snappedX && near(r.dx, -12.0f));
    b = {105, 20, 205, 120};
    r = snapMoving(movingRect(b), t, opts(2.0f));
    CHECK(!r.snappedX && near(r.dx, 0.0f));
    b = {103, 20, 203, 120};
    r = snapMoving(movingRect(b), t, opts(2.0f));
    CHECK(r.snappedX && near(r.dx, -3.0f));

    b = {-104, 20, -4, 120};
    r = snapMoving(movingRect(b), t, opts(1.0f));
    CHECK(r.snappedX && near(b.x1 + r.dx, 0.0f));

    b = {52, 300, 152, 400};
    r = snapMoving(movingRect(b), t, opts(1.0f));
    CHECK(r.snappedX && near(r.dx, -2.0f));

    b = {300, 97, 400, 197};
    r = snapMoving(movingRect(b), t, opts(1.0f));
    CHECK(r.snappedY && near(r.dy, 3.0f) && !r.snappedX);
}

void testSnapCorner()
{
    section("ima: canto entre 2 quads, com e sem zoom");
    const Point2 a[4] = {{20, 20}, {210, 10}, {200, 150}, {30, 160}};
    Targets t;
    addQuad(t, a);

    Result r = snapPoint({203, 147}, t, opts(1.0f));
    CHECK(r.snappedX && r.snappedY);
    CHECK(near(203 + r.dx, 200.0f) && near(147 + r.dy, 150.0f));
    CHECK(!r.marks.empty() && near(r.marks[0].x, 200.0f) && near(r.marks[0].y, 150.0f));

    r = snapPoint({203, 147}, t, opts(4.0f));
    CHECK(!r.snappedX && !r.snappedY);
    r = snapPoint({215, 135}, t, opts(0.25f));
    CHECK(r.snappedX && r.snappedY && near(215 + r.dx, 200.0f) && near(135 + r.dy, 150.0f));

    const Point2 slant[4] = {{0, 0}, {400, 100}, {400, 300}, {0, 200}};
    Targets ts;
    addQuad(ts, slant);
    r = snapPoint({150.0f, 43.0f}, ts, opts(1.0f));
    CHECK(r.snappedX && r.snappedY);
    CHECK(near(150.0f + r.dx, 151.3f, 0.1f) && near(43.0f + r.dy, 37.8f, 0.1f));
    const float onEdgeY = (150.0f + r.dx) / 4.0f;
    CHECK(near(43.0f + r.dy, onEdgeY, 0.01f));

    r = snapPoint({198, 300}, t, opts(1.0f));
    CHECK(r.snappedX && !r.snappedY && near(198 + r.dx, 200.0f));
}

void testThresholdScreenPx()
{
    section("ima: limiar em px de tela");
    Targets t;
    addRect(t, {0, 0, 100, 100});
    RectF b{106, 0, 206, 100};
    CHECK(snapMoving(movingRect(b), t, opts(1.0f)).snappedX);
    CHECK(!snapMoving(movingRect(b), t, opts(2.0f)).snappedX);
    b = {112, 0, 212, 100};
    CHECK(snapMoving(movingRect(b), t, opts(0.5f)).snappedX);
    Options o = opts(2.0f);
    b = {104, 0, 204, 100};
    CHECK(snapMoving(movingRect(b), t, o).snappedX);
    b = {104.5f, 0, 204.5f, 100};
    CHECK(!snapMoving(movingRect(b), t, o).snappedX);
    CHECK(near(opts(2.0f).threshold(), 4.0f));
}

void testAlt()
{
    section("ima: Alt desliga");
    Targets t;
    addRect(t, {0, 0, 100, 100});
    const RectF b{105, 0, 205, 100};
    Options o = opts(1.0f);
    CHECK(snapMoving(movingRect(b), t, o).snappedX);
    o.altHeld = true;
    Result r = snapMoving(movingRect(b), t, o);
    CHECK(!r.snappedX && !r.snappedY && r.guides.empty() && near(r.dx, 0.0f));
    CHECK(!snapPoint({103, 103}, t, o).snappedX);
    o.altHeld = false;
    o.enabled = false;
    CHECK(!snapMoving(movingRect(b), t, o).snappedX);
}

void testGrid()
{
    section("ima: grade");
    Targets t;
    Options o = opts(1.0f);
    o.gridOn = true;
    o.gridStep = 16.0f;
    Result r = snapMoving(movingRect({34, 49, 134, 149}), t, o);
    CHECK(r.snappedX && near(r.dx, -2.0f));
    CHECK(r.snappedY && near(r.dy, -1.0f));
    o.gridOn = false;
    CHECK(!snapMoving(movingRect({34, 49, 134, 149}), t, o).snappedX);
    o.gridOn = true;
    addRect(t, {0, 0, 37, 10});
    r = snapMoving(movingRect({40, 200, 100, 260}), t, o);
    CHECK(r.snappedX && near(40 + r.dx, 37.0f));
}

void testAlignDistribute()
{
    section("alinhar e distribuir com 3 slices");
    const std::vector<RectF> boxes = {{10, 20, 110, 70}, {150, 40, 200, 140}, {300, 0, 420, 60}};
    auto applied = [&](const std::vector<Point2>& off, size_t i) {
        return RectF{boxes[i].x0 + off[i].x, boxes[i].y0 + off[i].y, boxes[i].x1 + off[i].x, boxes[i].y1 + off[i].y};
    };

    std::vector<Point2> off = alignOffsets(boxes, Align::Left);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).x0, 10.0f));
    off = alignOffsets(boxes, Align::Right);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).x1, 420.0f));
    off = alignOffsets(boxes, Align::CenterH);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).cx(), 215.0f));
    off = alignOffsets(boxes, Align::Top);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).y0, 0.0f));
    off = alignOffsets(boxes, Align::Bottom);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).y1, 140.0f));
    off = alignOffsets(boxes, Align::MiddleV);
    for (size_t i = 0; i < 3; ++i) CHECK(near(applied(off, i).cy(), 70.0f));
    const RectF screen{0, 0, 1920, 1080};
    off = alignOffsets({boxes[0]}, Align::Right, &screen);
    CHECK(near(boxes[0].x1 + off[0].x, 1920.0f));

    off = distributeOffsets(boxes, Distribute::Horizontal);
    const RectF a = applied(off, 0), b = applied(off, 1), c = applied(off, 2);
    CHECK(near(a.x0, 10.0f) && near(c.x1, 420.0f));
    CHECK(near(b.x0 - a.x1, c.x0 - b.x1));
    CHECK(near(b.x0 - a.x1, ((420.0f - 10.0f) - (100.0f + 50.0f + 120.0f)) / 2.0f));
    off = distributeOffsets(boxes, Distribute::Vertical);
    const RectF a2 = applied(off, 0), b2 = applied(off, 1), c2 = applied(off, 2);
    CHECK(near(a2.y0 - c2.y1, b2.y0 - a2.y1));
    CHECK(near(distributeOffsets({boxes[0], boxes[1]}, Distribute::Horizontal)[0].x, 0.0f));
}

void testAbut()
{
    section("Encostar: bordas iguais ao pixel");
    const RectF left{0, 0, 300, 200};
    const RectF far{900, 40, 1100, 240};
    RectF mover{400, 50, 600, 250};
    Point2 off;
    CHECK(abutOffset(mover, {left, far}, Dir::Left, off));
    CHECK(mover.x0 + off.x == left.x1 && off.y == 0.0f);
    mover = {400, 50, 600, 250};
    CHECK(abutOffset(mover, {left, far}, Dir::Right, off));
    CHECK(mover.x1 + off.x == far.x0);
    const RectF top{380, 0, 700, 30};
    const RectF below{420, 400, 640, 500};
    mover = {400, 50, 600, 250};
    CHECK(abutOffset(mover, {top, below}, Dir::Up, off));
    CHECK(mover.y0 + off.y == top.y1 && off.x == 0.0f);
    CHECK(abutOffset(mover, {top, below}, Dir::Down, off));
    CHECK(mover.y1 + off.y == below.y0);
    mover = {250, 0, 450, 200};
    CHECK(abutOffset(mover, {left}, Dir::Left, off));
    CHECK(mover.x0 + off.x == left.x1);
    CHECK(!abutOffset(mover, {left}, Dir::Right, off));
    const RectF nearButAside{650, 500, 700, 600};
    const RectF overlapping{900, 60, 1000, 100};
    mover = {400, 50, 600, 250};
    CHECK(abutOffset(mover, {nearButAside, overlapping}, Dir::Right, off));
    CHECK(mover.x1 + off.x == overlapping.x0);
}

void testTransformPoints()
{
    section("transformar <-> pontos preserva a geometria do retangulo");
    using namespace mapedit;
    Point2 c[4] = {{100, 50}, {420, 50}, {420, 230}, {100, 230}};
    Xform x;
    CHECK(fromCorners(c, x));
    CHECK(near(x.x(), 100.0f) && near(x.y(), 50.0f) && near(x.w, 320.0f) && near(x.h, 180.0f) && x.rot == 0.0f);
    Point2 back[4];
    toCorners(x, back);
    for (int i = 0; i < 4; ++i) CHECK(back[i] == c[i]);

    Xform r;
    r.cx = 500;
    r.cy = 300;
    r.w = 200;
    r.h = 100;
    r.rot = degToRad(30.0f);
    Point2 rc[4];
    toCorners(r, rc);
    Xform r2;
    CHECK(fromCorners(rc, r2));
    CHECK(near(r2.cx, 500.0f, 0.01f) && near(r2.cy, 300.0f, 0.01f) && near(r2.w, 200.0f, 0.01f) && near(r2.h, 100.0f, 0.01f));
    CHECK(near(radToDeg(r2.rot), 30.0f, 0.01f));
    Point2 rc2[4];
    toCorners(r2, rc2);
    for (int i = 0; i < 4; ++i) CHECK(near(rc[i].x, rc2[i].x, 0.01f) && near(rc[i].y, rc2[i].y, 0.01f));

    const Point2 warp[4] = {{10, 10}, {300, 0}, {280, 200}, {0, 180}};
    Xform wx;
    CHECK(!fromCorners(warp, wx));
    CHECK(near(wx.x(), 0.0f) && near(wx.y(), 0.0f) && near(wx.w, 300.0f) && near(wx.h, 200.0f) && wx.rot == 0.0f);

    Xform s;
    s.cx = 150;
    s.cy = 100;
    s.w = 100;
    s.h = 50;
    ResizeOptions ro;
    Xform n = resizeXform(s, 4, 50, 30, ro);
    CHECK(near(n.x(), 100.0f) && near(n.y(), 75.0f) && near(n.w, 150.0f) && near(n.h, 80.0f));
    ro.keepAspect = true;
    n = resizeXform(s, 4, 50, 5, ro);
    CHECK(near(n.w / n.h, 2.0f) && near(n.w, 150.0f) && near(n.x(), 100.0f) && near(n.y(), 75.0f));
    ro = {};
    ro.fromCenter = true;
    n = resizeXform(s, 3, 20, 0, ro);
    CHECK(near(n.cx, 150.0f) && near(n.w, 140.0f) && near(n.h, 50.0f));
    ro = {};
    n = resizeXform(s, 3, -500, 0, ro);
    CHECK(near(n.w, 1.0f) && near(n.x(), 100.0f));
    Xform g = s;
    g.rot = degToRad(90.0f);
    const Point2 loc = toLocal(g, 0.0f, 40.0f);
    CHECK(near(loc.x, 40.0f) && near(loc.y, 0.0f, 1.0e-3f));
    n = resizeXform(g, 3, loc.x, loc.y, ro);
    CHECK(near(n.w, 140.0f, 0.01f) && near(n.cy, 120.0f, 0.01f) && near(n.cx, 150.0f, 0.01f));

    const float ang = rotateAngle({0, 0}, {100, 0}, {100, 30}, 0.0f, degToRad(15.0f));
    CHECK(near(radToDeg(ang), 15.0f, 0.01f));
    const float free = rotateAngle({0, 0}, {100, 0}, {0, 100}, 0.0f, 0.0f);
    CHECK(near(radToDeg(free), 90.0f, 0.01f));

    Xform rp;
    rp.cx = 100.4f; rp.cy = 50.4f; rp.w = 99.6f; rp.h = 49.7f;
    roundToPixels(rp);
    CHECK(near(rp.w, 100.0f) && near(rp.h, 50.0f) && near(rp.x(), std::round(rp.x())) && near(rp.y(), std::round(rp.y())));
}

OutputSetup makeSetup()
{
    OutputSetup s = OutputSetup::createDefault(1920, 1080, {});
    s.addSlice(0, 1920, 1080);
    return s;
}

void testHistory()
{
    section("desfazer e refazer de 5 operacoes");
    OutputSetup live = makeSetup();
    OutputHistory h(100);
    h.reset(live);
    CHECK(!h.canUndo() && !h.canRedo());

    std::vector<OutputSetup> states;
    states.push_back(live);
    for (int i = 1; i <= 5; ++i) {
        live.screens[0].slices[0].corners[kCornerTL].x = static_cast<float>(i * 10);
        CHECK(h.observe(live, false));
        states.push_back(live);
    }
    CHECK(h.undoCount() == 5 && !h.canRedo());
    CHECK(!h.observe(live, false));

    for (int i = 4; i >= 0; --i) {
        CHECK(h.undo(live));
        CHECK(live == states[static_cast<size_t>(i)]);
    }
    CHECK(!h.canUndo() && !h.undo(live) && h.redoCount() == 5);
    for (int i = 1; i <= 5; ++i) {
        CHECK(h.redo(live));
        CHECK(live == states[static_cast<size_t>(i)]);
    }
    CHECK(!h.canRedo() && !h.redo(live));

    h.undo(live);
    h.undo(live);
    CHECK(h.canRedo());
    live.screens[0].slices[1].name = "Outro";
    CHECK(h.observe(live, false));
    CHECK(!h.canRedo());

    live.screens[0].slices[0].opacity = 0.5f;
    CHECK(h.undo(live));
    CHECK(live.screens[0].slices[0].opacity == 1.0f && live.screens[0].slices[1].name == "Outro");
    CHECK(h.redo(live));
    CHECK(live.screens[0].slices[0].opacity == 0.5f);

    OutputHistory small(3);
    OutputSetup s2 = makeSetup();
    small.reset(s2);
    for (int i = 1; i <= 10; ++i) {
        s2.screens[0].slices[0].brightness = static_cast<float>(i) / 100.0f;
        small.observe(s2, false);
    }
    CHECK(small.undoCount() == 3);
    OutputHistory big(100);
    OutputSetup s3 = makeSetup();
    big.reset(s3);
    for (int i = 1; i <= 120; ++i) {
        s3.screens[0].slices[0].brightness = static_cast<float>(i) / 1000.0f;
        big.observe(s3, false);
    }
    CHECK(big.undoCount() == 100);
}

void testSliceSource()
{
    section("fonte do slice: desfazer e vistas de camada");
    OutputSetup live = makeSetup();
    OutputHistory h(100);
    h.reset(live);
    CHECK(live.screens[0].slices[0].source == kSliceFromComposition && live.screens[0].slices[0].layerId == 0);
    CHECK(collectLayerViews(live, live.screens[0].id).empty());

    OutputSetup before = live;
    Slice& a = live.screens[0].slices[0];
    a.source = kSliceFromLayer;
    a.layerId = 7;
    CHECK(h.observe(live, false));
    a.includeMaster = true;
    CHECK(h.observe(live, false));
    CHECK(h.undo(live) && !live.screens[0].slices[0].includeMaster && live.screens[0].slices[0].layerId == 7);
    CHECK(h.undo(live) && live == before);
    CHECK(h.redo(live) && live.screens[0].slices[0].source == kSliceFromLayer && live.screens[0].slices[0].layerId == 7);
    CHECK(h.redo(live) && live.screens[0].slices[0].includeMaster);

    Screen& screen = live.screens[0];
    const int copy = live.duplicateSlice(0, 0);
    CHECK(copy == 1 && screen.slices[1].source == kSliceFromLayer && screen.slices[1].layerId == 7);
    screen.slices[1].includeMaster = false;
    screen.kind = OutputKind::Spout;
    const std::vector<LayerViewRequest> views = collectLayerViews(live, -1);
    const LayerViewRequest withMaster{7, true}, withoutMaster{7, false};
    CHECK(views.size() == 2 && views[0] == withMaster && views[1] == withoutMaster);
    screen.slices[1].enabled = false;
    CHECK(collectLayerViews(live, -1).size() == 1);
    screen.slices[0].opacity = 0.0f;
    CHECK(collectLayerViews(live, -1).empty());
    screen.kind = OutputKind::Disabled;
    CHECK(collectLayerViews(live, -1).empty());
    CHECK(collectLayerViews(live, screen.id).size() == 2);
    screen.slices[0].layerId = 0;
    CHECK(collectLayerViews(live, screen.id).size() == 1);

    const std::vector<std::string> names = slicesLinkedToLayer(live, 7);
    CHECK(names.size() == 1 && names[0] == screen.name + " / " + screen.slices[1].name);
    CHECK(slicesLinkedToLayer(live, 8).empty() && slicesLinkedToLayer(live, 0).empty());
}

void testHistoryGesture()
{
    section("arraste continuo conta como 1 operacao");
    OutputSetup live = makeSetup();
    OutputHistory h(100);
    h.reset(live);
    for (int i = 1; i <= 60; ++i) {
        live.screens[0].slices[0].corners[kCornerTR].x = 1920.0f - static_cast<float>(i);
        CHECK(!h.observe(live, true));
    }
    CHECK(h.undoCount() == 0);
    CHECK(h.observe(live, false));
    CHECK(h.undoCount() == 1);
    CHECK(!h.observe(live, false));
    CHECK(h.undo(live));
    CHECK(live.screens[0].slices[0].corners[kCornerTR].x == 1920.0f);
    CHECK(!h.canUndo());
    CHECK(h.redo(live));
    CHECK(live.screens[0].slices[0].corners[kCornerTR].x == 1860.0f);

    for (int g = 0; g < 2; ++g) {
        for (int i = 1; i <= 10; ++i) {
            live.screens[0].slices[1].corners[kCornerBL].y += 1.0f;
            h.observe(live, true);
        }
        h.observe(live, false);
    }
    CHECK(h.undoCount() == 3);

    h.reset(live);
    CHECK(!h.canUndo() && !h.canRedo());
}

void testSliceUid()
{
    section("uid estavel de slice");
    OutputSetup setup;
    setup.addScreen(1920, 1080);
    setup.addSlice(0, 1920, 1080);
    setup.addSlice(0, 1920, 1080);
    const int u0 = setup.screens[0].slices[0].uid;
    const int u1 = setup.screens[0].slices[1].uid;
    const int u2 = setup.screens[0].slices[2].uid;
    CHECK(u0 > 0 && u1 > 0 && u2 > 0 && u0 != u1 && u1 != u2 && u0 != u2);

    setup.screens[0].slices[1].name = "Renomeado";
    std::swap(setup.screens[0].slices[0], setup.screens[0].slices[2]);
    CHECK(setup.screens[0].slices[0].uid == u2 && setup.screens[0].slices[2].uid == u0);
    CHECK(setup.screens[0].slices[1].uid == u1);
    CHECK(!ensureSliceUids(setup));

    const int dup = setup.duplicateSlice(0, 1);
    CHECK(dup == 2 && setup.screens[0].slices[2].uid != u1 && setup.screens[0].slices[2].uid > 0);

    setup.screens[0].slices.push_back(setup.screens[0].slices[0]);
    CHECK(ensureSliceUids(setup));
    CHECK(setup.screens[0].slices[0].uid == u2 && setup.screens[0].slices[4].uid != u2);

    setup.removeSlice(0, 4);
    const int added = setup.addSlice(0, 1920, 1080);
    CHECK(setup.screens[0].slices[static_cast<size_t>(added)].uid > setup.screens[0].slices[3].uid);

    OutputHistory h(10);
    h.reset(setup);
    OutputSetup live = setup;
    live.screens[0].slices.push_back(live.screens[0].slices[0]);
    ensureSliceUids(live);
    CHECK(h.observe(live, false));
    CHECK(!h.observe(live, false));
    CHECK(h.undoCount() == 1);
}

}

int main()
{
    testSnapEdge();
    testSnapCorner();
    testThresholdScreenPx();
    testAlt();
    testGrid();
    testAlignDistribute();
    testAbut();
    testTransformPoints();
    testHistory();
    testHistoryGesture();
    testSliceSource();
    testSliceUid();
    std::printf("\n%d verificacoes, %d falha(s)\n", g_checks, g_failures);
    std::printf(g_failures == 0 ? "MAPTEST OK\n" : "MAPTEST FALHOU\n");
    return g_failures == 0 ? 0 : 1;
}
