// BSL Slice Refiner (plugin FFGL "BSR3"): recorta o video nos formatos dos slices, ate 8 grupos com cantos arredondados,
// slices unidos (merge suave), borda com 12 efeitos, blinker, bounce e rotacao de cor. Reescrito como efeito nativo.
//
// Diferencas em relacao ao plugin:
//  - os slices vem do proprio BSL Stage (OutputSetup, publicados em SliceFeed); ainda aceita importar o
//    AdvancedOutput.xml do Resolume;
//  - o estado (o state.json do original) mora no parametro "state" do efeito e e salvo no projeto; cada instancia tem o
//    seu (o plugin tinha um estado global por processo);
//  - a interface web da porta 8083 virou o editor nativo do painel Clip; com "Controle remoto pela rede" ligado o mesmo
//    painel web (assets/bsl/web_bg/control.html) e servido por um servidor HTTP local, com a mesma API.

#include "fx/bsl/BslCommon.h"
#include "fx/bsl/BslEffects.h"
#include "fx/bsl/BslPreview.h"
#include "fx/bsl/SliceFeed.h"
#include "fx/bsl/SliceState.h"
#include "net/MiniHttp.h"

#include "app/Log.h"
#include "app/Paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

namespace bsl::fx::bsllight {

namespace {

using media::Json;
using media::ParamSet;
using namespace media::param;

constexpr int kMaxUsedSlices = 64;
constexpr double kPi = 3.14159265358979323846;

// Port do fragment shader original (GLSL 4.10 -> 3.30). 'pos' = pixels da textura, origem no canto superior esquerdo,
// y para baixo (a mesma convencao do AdvancedOutput.xml); o video entra premultiplicado, como nos plugins FFGL.
const char* const kHelpers = R"(
uniform float Opacity;
uniform float BlackBG;
uniform float Pass;
// slices: 2 vec4 cada. [0] = (cx, cy, hw, hh) px   [1] = (cos, sin, unused, fase do slice dentro do grupo)
uniform vec4 sl[128];
// grupos: 10 vec4 cada (ver SliceRefiner3.cpp)
uniform vec4 gd[80];

const float TAU = 6.28318530718;

float hash11( float p )
{
    p = fract( p * 0.1031 );
    p *= p + 33.33;
    p *= p + p;
    return fract( p );
}
float hash21( vec2 p )
{
    vec3 p3 = fract( vec3( p.xyx ) * 0.1031 );
    p3 += dot( p3, p3.yzx + 33.33 );
    return fract( ( p3.x + p3.y ) * p3.z );
}
float vnoise( vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );
    f = f * f * ( 3.0 - 2.0 * f );
    return mix( mix( hash21( i ), hash21( i + vec2( 1.0, 0.0 ) ), f.x ),
                mix( hash21( i + vec2( 0.0, 1.0 ) ), hash21( i + vec2( 1.0, 1.0 ) ), f.x ), f.y );
}
float fbm( vec2 p )
{
    float a = 0.5;
    float s = 0.0;
    for( int i = 0; i < 4; i++ )
    {
        s += a * vnoise( p );
        p = p * 2.03 + vec2( 17.1, 9.2 );
        a *= 0.5;
    }
    return s / 0.9375;
}
vec3 hueColor( float h )
{
    return clamp( abs( fract( h + vec3( 0.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 ) - 1.0, 0.0, 1.0 );
}
vec3 hueRot( vec3 c, float a )
{
    float cs = cos( a );
    float sn = sin( a );
    const vec3 k = vec3( 0.57735026 );
    return c * cs + cross( k, c ) * sn + k * dot( k, c ) * ( 1.0 - cs );
}
float smin( float a, float b, float k )
{
    float h = max( k - abs( a - b ), 0.0 ) / k;
    return min( a, b ) - h * h * k * 0.25;
}

// Rounded box, local frame with Y down. r = radii (TL, TR, BR, BL)
float sdBox( vec2 q, vec2 hb, vec4 r )
{
    float rr = q.x < 0.0 ? ( q.y < 0.0 ? r.x : r.w ) : ( q.y < 0.0 ? r.y : r.z );
    rr = min( rr, min( hb.x, hb.y ) );
    vec2 d = abs( q ) - hb + rr;
    return min( max( d.x, d.y ), 0.0 ) + length( max( d, 0.0 ) ) - rr;
}

// Position along the box perimeter in [0,1), clockwise from the top-left corner (Y down).
float perimU( vec2 q, vec2 hb, out float perim )
{
    hb = max( hb, vec2( 1.0 ) );
    perim = 4.0 * ( hb.x + hb.y );
    vec2 a = abs( q ) / hb;
    float t = max( max( a.x, a.y ), 1e-4 );
    vec2 b = q / t; // projected onto the boundary
    float s;
    if( a.y >= a.x )
    {
        if( q.y < 0.0 )
            s = b.x + hb.x;
        else
            s = 2.0 * hb.x + 2.0 * hb.y + ( hb.x - b.x );
    }
    else
    {
        if( q.x > 0.0 )
            s = 2.0 * hb.x + ( b.y + hb.y );
        else
            s = 4.0 * hb.x + 2.0 * hb.y + ( hb.y - b.y );
    }
    return fract( s / perim );
}

float band( float d, float c, float hw )
{
    return 1.0 - smoothstep( -0.5, 0.5, abs( d - c ) - hw );
}

float sh( float m, bool sm )
{
    return sm ? smoothstep( 0.2, 0.8, m ) : step( 0.5, m );
}

// ep: position relative to the group centre (already mirrored by symX/symY)
// u/perim: position along the perimeter of the nearest slice and its length in px
vec3 effectColor( int fx, vec2 ep, float u, float perim, float t, float var, bool sm, vec3 A, vec3 B, vec2 ext, int sym )
{
    float fr = 0.5 + 2.0 * var; // detail / frequency multiplier
    float m = 0.0;
    vec3 col = A;
    if( fx == 0 ) // noise
    {
        float n = fbm( ep * ( 0.012 * fr ) + vec2( t * 0.35, -t * 0.22 ) );
        m = sh( clamp( ( n - 0.5 ) * 2.4 + 0.5, 0.0, 1.0 ), sm );
        col = mix( B, A, m );
    }
    else if( fx == 1 ) // spiral, travelling inwards
    {
        float ang = atan( ep.y, ep.x + 1e-5 );
        float arms = floor( 1.0 + fr * 2.0 );
        float ph = ang / TAU * arms + length( ep ) / ( 70.0 / fr ) + t;
        m = sh( 0.5 + 0.5 * sin( TAU * ph ), sm );
        col = mix( B, A, m );
    }
    else if( fx == 2 ) // radial waves going outwards
    {
        float ph = length( ep ) / ( 60.0 / fr ) - t;
        m = sh( 0.5 + 0.5 * sin( TAU * ph ), sm );
        col = mix( B, A, m );
    }
    else if( fx == 3 ) // zebra
    {
        float ph = dot( ep, vec2( 0.70711 ) ) / ( 40.0 / fr ) - t;
        m = sm ? smoothstep( 0.3, 0.7, 0.5 + 0.5 * sin( TAU * ph ) ) : step( 0.5, fract( ph ) );
        col = mix( B, A, m );
    }
    else if( fx == 4 ) // disco squares
    {
        float cs = 36.0 / fr;
        vec2 c = floor( ep / cs );
        vec2 f = fract( ep / cs );
        float st = t * 1.5;
        float ti = floor( st );
        float ff = fract( st );
        float h0 = hash21( c + ti * 7.13 );
        float h1 = hash21( c + ( ti + 1.0 ) * 7.13 );
        float h = sm ? mix( h0, h1, smoothstep( 0.0, 1.0, ff ) ) : h0;
        m = sm ? smoothstep( 0.35, 0.65, h ) : step( 0.5, h );
        vec2 e = min( f, 1.0 - f );
        float grid = smoothstep( 0.0, 0.08, min( e.x, e.y ) );
        col = mix( B, A, m ) * mix( 0.55, 1.0, grid );
    }
    else if( fx == 5 ) // chaser: a segment running around the shape
    {
        float L = mix( 0.12, 0.40, var );
        float y = fract( t * 0.4 - u );
        if( y < L )
            m = sm ? pow( 1.0 - y / L, 1.3 ) : 1.0;
        col = mix( B, A, m );
    }
    else if( fx == 6 ) // marching dashes
    {
        float n = max( floor( perim / ( 36.0 / fr ) ), 2.0 );
        float ph = fract( u * n - t * 0.5 );
        m = sm ? ( 1.0 - smoothstep( 0.45, 0.6, ph ) ) * smoothstep( 0.0, 0.1, ph ) : step( ph, 0.55 );
        col = mix( B, A, m );
    }
    else if( fx == 7 ) // rainbow turning around the shape
    {
        float cycles = floor( 1.0 + fr );
        float h = fract( u * cycles - t * 0.15 );
        col = hueColor( sm ? h : floor( h * 6.0 ) / 6.0 );
    }
    else if( fx == 8 ) // sparkle
    {
        float cs = 22.0 / fr;
        vec2 c = floor( ep / cs );
        vec2 f = fract( ep / cs );
        float h = hash21( c );
        if( h > 0.35 )
        {
            vec2 cen = vec2( 0.5 ) + ( vec2( hash21( c + 3.1 ), hash21( c + 7.7 ) ) - 0.5 ) * 0.5;
            float dd = length( f - cen );
            vec2 df = abs( f - cen );
            float tw = pow( 0.5 + 0.5 * sin( t * 5.0 * ( 0.6 + h ) + h * 50.0 ), 3.0 );
            float star = smoothstep( 0.32, 0.0, dd ) + 0.6 * smoothstep( 0.04, 0.0, min( df.x, df.y ) ) * smoothstep( 0.5, 0.0, dd );
            m = clamp( star * tw * 1.5, 0.0, 1.0 );
        }
        if( !sm )
            m = step( 0.3, m );
        col = mix( B, A, m ) + vec3( m * m * 0.5 );
    }
    else if( fx == 9 ) // glitch colours (the jitter itself is applied to the contour)
    {
        float bn = floor( ep.y / 6.0 ) + floor( ep.x / 60.0 ) * 7.0;
        float fl = hash21( vec2( bn, floor( t * 14.0 ) ) );
        m = step( 0.72, fl );
        col = mix( A, B, m );
        if( fl > 0.93 )
            col = vec3( 1.0 );
    }
    else if( fx == 10 ) // pulse (breathing)
    {
        float br = 0.5 + 0.5 * sin( TAU * ( t * 0.5 - length( ep ) * var / 400.0 ) );
        col = mix( A, B, sm ? br : step( 0.5, br ) );
        col *= mix( 0.7, 1.25, br );
    }
    else // scanner: bright bar sweeping across the group
    {
        float xn = ep.x / max( ext.x, 1.0 );
        float tri = abs( fract( t * 0.25 ) * 2.0 - 1.0 );
        float c = ( sym & 1 ) != 0 ? tri : tri * 2.0 - 1.0;
        float wn = mix( 0.06, 0.3, var );
        float g = ( xn - c ) / wn;
        m = exp( -g * g );
        if( !sm )
            m = step( 0.5, m );
        col = mix( B, A, m ) + vec3( m * m * m * 0.4 );
    }
    return clamp( col, 0.0, 1.0 );
}
)";

const char* const kMain = R"(
void main()
{
    vec2 pos = imgPos() * uSize;
    vec4 vid = sampP( imgPos() );
    if( Pass > 0.5 )
    {
        fragColor = samp( imgPos() );
        return;
    }
    vec4 res = BlackBG > 0.5 ? vec4( 0.0, 0.0, 0.0, 1.0 ) : vec4( 0.0 );

    for( int g = 0; g < 8; g++ )
    {
        vec4 g0 = gd[ g * 10 + 0 ]; // fill(1 video,0 none,-1 skip), masterSize, offset, mergeSmooth
        if( g0.x < -0.5 )
            continue;
        vec4 g1 = gd[ g * 10 + 1 ]; // corners TL TR BR BL (0..1)
        vec4 g2 = gd[ g * 10 + 2 ]; // merge, softEdge px, stroke width, glow
        vec4 g3 = gd[ g * 10 + 3 ]; // colour A rgb, effect mode
        vec4 g4 = gd[ g * 10 + 4 ]; // colour B rgb, effect index
        vec4 g5 = gd[ g * 10 + 5 ]; // stroke time, variation, smooth, sym bits
        vec4 g6 = gd[ g * 10 + 6 ]; // dual on, dual width, dual gap, blink factor
        vec4 g7 = gd[ g * 10 + 7 ]; // hue shift (turns), bounce mode (0 off,1 smooth,2 step), bounce min, multi
        vec4 g8 = gd[ g * 10 + 8 ]; // bounce phase, group centre xy
        vec4 g9 = gd[ g * 10 + 9 ]; // first slice, slice count, group half extents
        int start = int( g9.x + 0.5 );
        int cnt = int( g9.y + 0.5 );
        if( cnt <= 0 )
            continue;

        bool merged = g2.x > 0.5;
        bool effectMode = g3.w > 0.5;
        float k = g0.w;
        float offs = g0.z;
        float t = g5.x;
        int fx = int( g4.w + 0.5 );
        int sym = int( g5.w + 0.5 );
        float sw = g2.z;
        if( fx == 10 && effectMode )
            sw *= mix( 0.5, 1.15, 0.5 + 0.5 * sin( TAU * t * 0.5 ) );
        bool hasStroke = sw > 0.05;
        float hw2 = sw * 0.5;
        float dualW = g6.y;
        bool dualOn = g6.x > 0.5 && dualW > 0.05;
        float dualC = -( hw2 + g6.z + dualW * 0.5 );
        float jit = 0.0;
        if( fx == 9 && effectMode )
        {
            vec2 cell = floor( pos / vec2( 48.0, 8.0 ) );
            float ts = floor( t * 8.0 );
            float on = step( 0.55, hash21( cell + ts * 13.7 + 31.0 ) );
            jit = ( hash21( cell + ts * 7.3 ) - 0.5 ) * 2.0 * ( hw2 + 4.0 ) * on;
        }

        float dU = 1e5;
        float cov = 0.0;
        float dcov = 0.0;
        float dEdge = 1e5;
        float nd = 1e5;
        vec2 nq = vec2( 0.0 );
        vec2 nhb = vec2( 1.0 );
        float cut = 360.0 + abs( offs ) + k + sw;

        for( int i = start; i < start + cnt; i++ )
        {
            vec4 s0 = sl[ i * 2 ];
            vec4 s1 = sl[ i * 2 + 1 ];
            vec2 p = pos - s0.xy;
            vec2 hb = s0.zw;
            vec2 q = vec2( 0.0 );
            float d;
            float lb = length( p ) - length( hb );
            if( lb > cut )
                d = lb; // far away: conservative distance, nothing to draw here
            else
            {
                q = vec2( dot( p, s1.xy ), dot( p, vec2( -s1.y, s1.x ) ) );
                float sc = 1.0;
                if( g7.y > 0.5 )
                {
                    float ph = g8.x + ( g7.w > 0.5 ? s1.w : 0.0 );
                    float w = g7.y < 1.5 ? 0.5 - 0.5 * cos( TAU * ph ) : step( 0.5, fract( ph ) );
                    sc = mix( 1.0, g7.z, w );
                }
                hb *= sc;
                float rr = g0.y * min( hb.x, hb.y );
                d = sdBox( q, hb, g1 * rr ) + offs;
            }
            dU = ( merged && k > 0.0 ) ? smin( dU, d, k ) : min( dU, d );
            float ad = abs( d );
            if( ad < nd )
            {
                nd = ad;
                nq = q;
                nhb = hb;
            }
            if( !merged )
            {
                if( hasStroke )
                    cov = max( cov, band( d + jit, 0.0, hw2 ) );
                if( dualOn )
                    dcov = max( dcov, band( d + jit, dualC, dualW * 0.5 ) );
                dEdge = min( dEdge, ad );
            }
        }
        if( merged )
        {
            if( hasStroke )
                cov = band( dU + jit, 0.0, hw2 );
            if( dualOn )
                dcov = band( dU + jit, dualC, dualW * 0.5 );
            dEdge = abs( dU );
        }

        // ---- fill ----
        float inside = 1.0 - smoothstep( -0.5, 0.5, dU );
        if( g0.x > 0.5 && inside > 0.001 )
        {
            vec3 vc = vid.rgb;
            if( g2.y > 0.5 )
                vc *= smoothstep( 0.0, g2.y, -dU ); // inner shadow
            vec4 src = vec4( vc, vid.a ) * inside;
            res = src + res * ( 1.0 - src.a );
        }

        // ---- stroke / glow ----
        float blink = g6.w;
        float glowK = g2.w;
        float glowA = 0.0;
        if( glowK > 0.001 )
            glowA = glowK * exp( -max( dEdge - hw2, 0.0 ) / ( 6.0 + 34.0 * glowK ) ) * 0.9;
        float strokeA = max( cov, dcov );
        if( ( strokeA > 0.002 || glowA > 0.002 ) && blink > 0.002 )
        {
            vec2 ep = pos - g8.yz;
            vec2 qq = nq;
            if( ( sym & 1 ) != 0 )
            {
                ep.x = abs( ep.x );
                qq.x = abs( qq.x );
            }
            if( ( sym & 2 ) != 0 )
            {
                ep.y = abs( ep.y );
                qq.y = abs( qq.y );
            }
            vec3 col = g3.rgb;
            if( effectMode )
            {
                float perim;
                float u = perimU( qq, nhb, perim );
                col = effectColor( fx, ep, u, perim, t, g5.y, g5.z > 0.5, g3.rgb, g4.rgb, g9.zw, sym );
            }
            if( g7.x != 0.0 )
                col = hueRot( col, TAU * g7.x );

            if( glowA > 0.002 )
            {
                float ga = glowA * blink;
                res.rgb += col * ga;
                res.a = max( res.a, ga );
            }
            float a = strokeA * blink;
            vec4 src = vec4( col * a, a );
            res = src + res * ( 1.0 - a );
        }
    }

    vec4 o = mix( vid, res, clamp( Opacity, 0.0, 1.0 ) );
    fragColor = toStraight( o.rgb, o.a );
}
)";

double smoothStep(double a, double b, double x)
{
    const double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    return t * t * (3.0 - 2.0 * t);
}

double fract(double x)
{
    return x - std::floor(x);
}

// Fases de animacao de um grupo
struct GroupPhase
{
    double stroke = 0.0;
    double blink = 0.0;
    double bounce = 0.0;
    double hue = 0.0;
};

std::string localAddresses();

class SliceRefiner3Effect final : public BslEffect
{
public:
    SliceRefiner3Effect() : BslEffect({kSliceRefiner3Id, "BSL Slice Refiner", "Slices"}, describeSliceRefiner3())
    {
        for (float& b : blinkFactor_)
            b = 1.0f;
        stateJson_ = defaultSliceStateJson();
    }

    ~SliceRefiner3Effect() override { stopRemote(); }

    void setParams(const ParamSet& params) override
    {
        BslEffect::setParams(params);
        const Json raw = params.raw("state");
        std::lock_guard lk(mutex_);
        if (raw != lastParamState_) { // so reinterpreta quando o valor do projeto mudou
            lastParamState_ = raw;
            SliceState parsed;
            if (raw.is_object() && sliceStateFromJson(raw, parsed)) {
                state_ = std::move(parsed);
                stateJson_ = sliceStateToJson(state_);
                ++stateRev_;
                snapshotDirty_ = true;
            }
        }
        const Json rawReload = params.raw("reload");
        const int reload = rawReload.is_number_integer() ? rawReload.get<int>() : 0;
        if (reload != lastReload_) {
            if (lastReload_ >= 0)
                reloadRequested_ = true;
            lastReload_ = reload;
        }
    }

    // O painel remoto mudou o estado: devolve ao parametro (salvo no projeto)
    bool pollParamChanges(media::ParamValues& values) override
    {
        std::lock_guard lk(mutex_);
        if (!remoteDirty_)
            return false;
        remoteDirty_ = false;
        lastParamState_ = stateJson_;
        values.set("state", stateJson_);
        return true;
    }

    media::SourceStatus status() const override
    {
        media::SourceStatus s = BslEffect::status();
        if (!s.text.empty())
            return s;
        std::lock_guard lk(mutex_);
        if (!remoteError_.empty())
            return {media::SourceStatus::Level::Warning, "Controle remoto: " + remoteError_};
        if (remoteRunning_)
            return {media::SourceStatus::Level::Info,
                    "Controle remoto ligado: http://localhost:" + std::to_string(remotePort_) + "/ " + remoteHint_};
        return {};
    }

protected:
    std::string fragmentSource() const override { return std::string(kBslPrelude) + kHelpers + kMain; }

    void prepare(const EffectContext& ctx, int /*w*/, int /*h*/) override
    {
        syncRemote(ctx);
        refreshSlices(ctx);
        advanceClocks(ctx);
    }

    void render(const EffectContext& ctx, int w, int h) override
    {
        const ParamSet& p = params();
        SliceState st;
        {
            std::lock_guard lk(mutex_);
            st = state_;
        }

        // ---- escala do canvas (ou da composicao do XML) para a textura ----
        float sx = 1.0f, sy = 1.0f;
        if (compW_ > 0 && compH_ > 0 && w > 0 && h > 0) {
            sx = static_cast<float>(w) / static_cast<float>(compW_);
            sy = static_cast<float>(h) / static_cast<float>(compH_);
            if (std::fabs(sx - 1.0f) < 0.002f)
                sx = 1.0f;
            if (std::fabs(sy - 1.0f) < 0.002f)
                sy = 1.0f;
        }

        // slice -> grupo
        std::vector<int> groupOf(slices_.size(), -1);
        for (size_t i = 0; i < slices_.size(); ++i) {
            const auto it = st.assign.find(slices_[i].id);
            if (it != st.assign.end())
                groupOf[i] = it->second;
        }

        float sl[kMaxUsedSlices * 8] = {};
        float gd[kSliceGroups * 40] = {};
        int used = 0;
        for (int g = 0; g < kSliceGroups; ++g) {
            float* G = gd + g * 40;
            const SliceGroup& grp = st.groups[g];
            G[0] = -1.0f; // ignorado, a menos que seja preenchido abaixo
            if (!grp.enabled)
                continue;
            std::vector<size_t> idx;
            for (size_t i = 0; i < groupOf.size(); ++i)
                if (groupOf[i] == g && used + static_cast<int>(idx.size()) < kMaxUsedSlices)
                    idx.push_back(i);
            if (idx.empty())
                continue;

            const float n = static_cast<float>(idx.size());
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f, minSide = 1e9f;
            for (size_t k = 0; k < idx.size(); ++k) {
                const FeedSlice& s = slices_[idx[k]];
                // centro e meias dimensoes escalados (a rotacao e mantida)
                const float cx = s.cx * sx, cy = s.cy * sy, hw = s.hw * sx, hh = s.hh * sy;
                const float a = static_cast<float>(s.angle * kPi / 180.0);
                const float c = std::cos(a), si = std::sin(a);
                float* S = sl + (used + static_cast<int>(k)) * 8;
                S[0] = cx;
                S[1] = cy;
                S[2] = hw;
                S[3] = hh;
                S[4] = c;
                S[5] = si;
                S[6] = 0.0f;
                S[7] = static_cast<float>(k) / n;
                for (int sxn = -1; sxn <= 1; sxn += 2)
                    for (int syn = -1; syn <= 1; syn += 2) {
                        const float px = cx + static_cast<float>(sxn) * hw * c - static_cast<float>(syn) * hh * si;
                        const float py = cy + static_cast<float>(sxn) * hw * si + static_cast<float>(syn) * hh * c;
                        minX = std::min(minX, px);
                        maxX = std::max(maxX, px);
                        minY = std::min(minY, py);
                        maxY = std::max(maxY, py);
                    }
                minSide = std::min(minSide, 2.0f * std::min(hw, hh));
            }

            const GroupPhase& ph = phase_[g];
            G[0] = grp.fillVideo ? 1.0f : 0.0f;
            G[1] = grp.masterSize;
            G[2] = grp.offset;
            // Correcao em relacao ao original: com "Suavizar juncao" = 0 a uniao por minimo deixava uma linha de borda nas
            // emendas entre slices encostados (nos dois lados da emenda a distancia ate a borda e ~0). A uniao suave
            // some com a linha quando o raio e maior que 4x a meia-largura da borda; usa-se esse minimo.
            const float seamBand = grp.strokeWidth * 0.5f + (grp.dualOn ? grp.dualGap + grp.dualWidth : 0.0f) + 1.0f;
            G[3] = grp.merge ? std::max(grp.mergeSmooth, grp.strokeWidth > 0.05f ? 4.0f * seamBand : 0.0f) : 0.0f;
            for (int k = 0; k < 4; ++k)
                G[4 + k] = grp.equalCorners ? grp.corners[0] : grp.corners[k];
            G[8] = grp.merge ? 1.0f : 0.0f;
            G[9] = grp.softEdge * minSide;
            G[10] = grp.strokeWidth;
            G[11] = grp.glow;
            G[12] = grp.color[0];
            G[13] = grp.color[1];
            G[14] = grp.color[2];
            G[15] = grp.strokeEffectMode ? 1.0f : 0.0f;
            G[16] = grp.colorB[0];
            G[17] = grp.colorB[1];
            G[18] = grp.colorB[2];
            G[19] = static_cast<float>(grp.effect);
            G[20] = static_cast<float>(ph.stroke);
            G[21] = grp.variation;
            G[22] = grp.smooth ? 1.0f : 0.0f;
            G[23] = (grp.symX ? 1.0f : 0.0f) + (grp.symY ? 2.0f : 0.0f);
            G[24] = grp.dualOn ? 1.0f : 0.0f;
            G[25] = grp.dualWidth;
            G[26] = grp.dualGap;
            G[27] = blinkFactor_[g];
            G[28] = grp.rotOn ? static_cast<float>(fract(ph.hue)) : 0.0f;
            G[29] = grp.bounceMode == SliceMode::Off ? 0.0f : (grp.bounceSmooth ? 1.0f : 2.0f);
            G[30] = grp.bounceMin;
            G[31] = grp.bounceMulti ? 1.0f : 0.0f;
            G[32] = static_cast<float>(fract(ph.bounce));
            G[33] = (minX + maxX) * 0.5f;
            G[34] = (minY + maxY) * 0.5f;
            G[35] = 0.0f;
            G[36] = static_cast<float>(used);
            G[37] = n;
            G[38] = (maxX - minX) * 0.5f;
            G[39] = (maxY - minY) * 0.5f;
            used += static_cast<int>(idx.size());
        }

        Shader& sh = shader();
        sh.setFloat("Opacity", p.f("opacity") * 0.01f);
        sh.setFloat("BlackBG", p.b("blackBg") ? 1.0f : 0.0f);
        sh.setFloat("Pass", used == 0 ? 1.0f : 0.0f);
        if (used > 0) {
            const GLuint prog = sh.id();
            glUniform4fv(glGetUniformLocation(prog, "sl"), used * 2, sl);
            glUniform4fv(glGetUniformLocation(prog, "gd"), kSliceGroups * 10, gd);
        }
        (void)ctx;
    }

    void afterDraw(const EffectContext& ctx, int w, int h, const Fbo& output) override
    {
        if (remoteRunning() && preview_.wanted())
            preview_.capture(output.texture(), w, h, ctx.topOrigin);
    }

private:
    // ----------------------------------------------------------------------------------------- slices
    void refreshSlices(const EffectContext& ctx)
    {
        const ParamSet& p = params();
        if (p.s("source") == "xml") {
            std::string path = p.s("xmlFile");
            if (path.empty())
                path = defaultResolumeSliceXmlPath();
            using Clock = std::chrono::steady_clock;
            const auto now = Clock::now();
            const std::filesystem::path fp{std::u8string(path.begin(), path.end())};
            bool reread = path != xmlPath_ || fromFeed_ || reloadRequestedLocal();
            if (!reread && now - lastXmlRead_ > std::chrono::seconds(2)) {
                lastXmlRead_ = now;
                std::error_code ec;
                const auto mtime = std::filesystem::last_write_time(fp, ec);
                reread = !ec && mtime != xmlTime_;
            }
            if (!reread)
                return;
            lastXmlRead_ = now;
            xmlPath_ = path;
            fromFeed_ = false;
            std::vector<FeedSlice> parsed;
            int xw = 0, xh = 0;
            std::error_code ec;
            xmlTime_ = std::filesystem::last_write_time(fp, ec);
            if (ec)
                xmlTime_ = std::filesystem::file_time_type{};
            std::ifstream in(fp, std::ios::binary);
            if (in) {
                const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (text.size() < (64u << 20))
                    parsed = parseResolumeSlices(text, &xw, &xh);
            }
            applySlices(std::move(parsed), xw > 0 ? xw : ctx.canvasWidth, xh > 0 ? xh : ctx.canvasHeight);
            return;
        }
        // Saida do BSL Stage
        SliceFeed& feed = SliceFeed::instance();
        const bool reload = reloadRequestedLocal();
        if (!fromFeed_ || feed.version() != feedVersion_ || reload) {
            fromFeed_ = true;
            xmlPath_.clear();
            feedVersion_ = feed.version();
            applySlices(feed.slices(), feed.canvasWidth(), feed.canvasHeight());
        }
    }

    bool reloadRequestedLocal()
    {
        std::lock_guard lk(mutex_);
        const bool r = reloadRequested_;
        reloadRequested_ = false;
        return r;
    }

    void applySlices(std::vector<FeedSlice> next, int cw, int ch)
    {
        std::lock_guard lk(mutex_);
        slices_ = std::move(next);
        compW_ = cw;
        compH_ = ch;
        snapshotDirty_ = true;
    }

    // ----------------------------------------------------------------------------------------- relogios
    void advanceClocks(const EffectContext& ctx)
    {
        const ParamSet& p = params();
        double dt = haveLast_ ? ctx.time - lastTime_ : 0.0;
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 0.25)
            dt = 0.25;
        lastTime_ = ctx.time;
        haveLast_ = true;
        bpm_.store(ctx.bpm);

        const double master = static_cast<double>(p.f("masterSpeed"));
        const bool forceBlink = p.b("blinker");
        const double beats = ctx.beat;

        SliceState st;
        {
            std::lock_guard lk(mutex_);
            st = state_;
        }
        for (int g = 0; g < kSliceGroups; ++g) {
            const SliceGroup& G = st.groups[g];
            GroupPhase& ph = phase_[g];
            ph.stroke = std::fmod(ph.stroke + dt * G.speed * master * 2.0, 65536.0);

            SliceMode bm = G.blinkMode;
            float rate = G.blinkRate;
            if (bm == SliceMode::Off && forceBlink) {
                bm = SliceMode::Speed;
                rate = 2.0f;
            }
            if (bm == SliceMode::Speed)
                ph.blink = std::fmod(ph.blink + dt * rate * master, 1024.0);
            else if (bm == SliceMode::Bpm)
                ph.blink = beats / std::max(0.05f, rate);
            if (bm == SliceMode::Off) {
                blinkFactor_[g] = 1.0f;
            } else {
                const double s = fract(ph.blink);
                blinkFactor_[g] = static_cast<float>(smoothStep(0.0, 0.06, s) * (1.0 - smoothStep(0.5, 0.56, s)));
            }

            if (G.bounceMode == SliceMode::Speed)
                ph.bounce = std::fmod(ph.bounce + dt * G.bounceSpeed * master, 1024.0);
            else if (G.bounceMode == SliceMode::Bpm)
                ph.bounce = beats / std::max(0.125f, G.bounceBeats);

            if (G.rotOn) {
                if (G.rotBars >= 1.0f)
                    ph.hue = beats / (4.0 * static_cast<double>(G.rotBars));
                else
                    ph.hue = std::fmod(ph.hue + dt * G.rotSpeed * master, 1024.0);
            }
        }
    }

    // ----------------------------------------------------------------------------------------- controle remoto
    bool remoteRunning() const
    {
        std::lock_guard lk(mutex_);
        return remoteRunning_;
    }

    void stopRemote()
    {
        server_.stop();
        std::lock_guard lk(mutex_);
        remoteRunning_ = false;
    }

    void syncRemote(const EffectContext& ctx)
    {
        const ParamSet& p = params();
        const bool want = p.b("remote") && !ctx.preview;
        const int port = p.i("remotePort");
        if (!want) {
            if (server_.running())
                stopRemote();
            std::lock_guard lk(mutex_);
            remoteError_.clear();
            return;
        }
        if (server_.running() && server_.port() == port)
            return;
        using Clock = std::chrono::steady_clock;
        const auto now = Clock::now();
        if (server_.running())
            stopRemote();
        else if (now - lastRemoteTry_ < std::chrono::seconds(2))
            return; // porta ocupada: tenta de novo a cada 2 s, sem travar o quadro
        lastRemoteTry_ = now;
        std::string error;
        if (server_.start(port, [this](const net::HttpRequest& rq, net::HttpResponse& rs) { handle(rq, rs); }, &error)) {
            std::lock_guard lk(mutex_);
            remoteRunning_ = true;
            remotePort_ = port;
            remoteError_.clear();
            remoteHint_ = localAddresses();
            snapshotDirty_ = true;
            log::info("Slice Refiner: controle remoto em http://localhost:" + std::to_string(port) + "/");
        } else {
            std::lock_guard lk(mutex_);
            remoteError_ = error;
        }
    }

    Json slicesJson()
    {
        std::lock_guard lk(mutex_);
        Json root = Json::object();
        root["v"] = 1;
        root["comp"] = Json{{"w", compW_ > 0 ? compW_ : 1920}, {"h", compH_ > 0 ? compH_ : 1080}};
        auto r1 = [](float v) { return std::floor(static_cast<double>(v) * 100.0 + 0.5) / 100.0; };
        Json arr = Json::array();
        for (const FeedSlice& s : slices_) {
            Json o = Json::object();
            o["id"] = s.id;
            o["name"] = s.sliceName.empty() ? s.name : s.sliceName;
            o["screen"] = s.screen;
            // Retangulo orientado SEM rotacao: centro = (x + w/2, y + h/2), girado de 'angle' graus em torno dele
            o["x"] = r1(s.cx - s.hw);
            o["y"] = r1(s.cy - s.hh);
            o["w"] = r1(s.hw * 2.0f);
            o["h"] = r1(s.hh * 2.0f);
            o["angle"] = r1(s.angle);
            arr.push_back(std::move(o));
        }
        root["slices"] = std::move(arr);
        root["state"] = stateJson_;
        const float bpm = bpm_.load();
        root["bpm"] = bpm > 1.0f ? std::floor(static_cast<double>(bpm) * 100.0 + 0.5) / 100.0 : 120.0;
        return root;
    }

    void handle(const net::HttpRequest& rq, net::HttpResponse& rs)
    {
        const std::string& path = rq.path;
        if (rq.method == "GET" && (path == "/" || path == "/control.html" || path == "/index.html")) {
            std::string html;
            const std::filesystem::path file = paths::assetsDir() / L"bsl" / L"web_bg" / L"control.html";
            if (!net::readWholeFile(file, html)) {
                rs.text("control.html não encontrado em assets/bsl/web_bg", 404);
                return;
            }
            rs.status = 200;
            rs.contentType = "text/html; charset=utf-8";
            rs.body = std::move(html);
            return;
        }
        if (rq.method == "GET" && path == "/api/slices") {
            rs.json(slicesJson().dump());
            return;
        }
        if (rq.method == "POST" && path == "/api/slices/state") {
            Json body = Json::parse(rq.body, nullptr, false);
            SliceState next;
            std::string error;
            if (body.is_discarded()) {
                rs.json("{\"ok\":false,\"error\":\"JSON inválido\"}", 400);
                return;
            }
            if (!sliceStateFromJson(body, next, &error)) {
                rs.json(Json{{"ok", false}, {"error", error}}.dump(), 400);
                return;
            }
            {
                std::lock_guard lk(mutex_);
                state_ = std::move(next);
                stateJson_ = sliceStateToJson(state_);
                ++stateRev_;
                remoteDirty_ = true;
            }
            rs.json("{\"ok\":true}");
            return;
        }
        if ((rq.method == "POST" || rq.method == "GET") && path == "/api/slices/reload") {
            {
                std::lock_guard lk(mutex_);
                reloadRequested_ = true;
            }
            rs.json(slicesJson().dump());
            return;
        }
        if (rq.method == "GET" && path == "/api/slices/screen") {
            std::string jpeg;
            if (!preview_.jpeg(jpeg)) {
                rs.status = 204;
                return;
            }
            rs.status = 200;
            rs.contentType = "image/jpeg";
            rs.body = std::move(jpeg);
            return;
        }
        rs.json("{\"ok\":false,\"error\":\"not found\"}", 404);
    }

    // ----------------------------------------------------------------------------------------- dados
    mutable std::mutex mutex_;
    SliceState state_;
    Json stateJson_;
    Json lastParamState_;
    std::uint64_t stateRev_ = 0;
    bool remoteDirty_ = false;
    bool snapshotDirty_ = false;
    bool reloadRequested_ = false;
    int lastReload_ = -1;

    std::vector<FeedSlice> slices_;
    int compW_ = 0, compH_ = 0;
    bool fromFeed_ = false;
    std::uint64_t feedVersion_ = ~0ull;
    std::string xmlPath_;
    std::filesystem::file_time_type xmlTime_{};
    std::chrono::steady_clock::time_point lastXmlRead_{};

    GroupPhase phase_[kSliceGroups];
    float blinkFactor_[kSliceGroups];
    double lastTime_ = 0.0;
    bool haveLast_ = false;
    std::atomic<float> bpm_{120.0f};

    net::HttpServer server_;
    PreviewCapture preview_;
    bool remoteRunning_ = false;
    int remotePort_ = 0;
    std::string remoteError_;
    std::string remoteHint_;
    std::chrono::steady_clock::time_point lastRemoteTry_{};
};

std::string localAddresses()
{
    return "(no celular ou tablet: o IP deste computador na mesma rede, nessa porta)";
}

} // namespace

std::vector<media::ParamDesc> describeSliceRefiner3()
{
    std::vector<media::ParamDesc> d;
    d.push_back(makeCustom("state", "Grupos", "bsl-slices", defaultSliceStateJson(), "Grupos de slices"));
    d.back().tooltip = "Atribua os slices a até 8 grupos e ajuste formato, borda, efeito e animação de cada um";

    d.push_back(makeEnum("source", "Origem dos slices",
                         {{"output", "Saída do BSL Stage"}, {"xml", "XML do Resolume"}}, "output", "Slices"));
    d.back().tooltip = "Saída: os slices da aba Saída / Mapeamento do projeto. XML: importa um AdvancedOutput.xml do Resolume";
    d.push_back(makeFile("xmlFile", "XML do Resolume", "XML do Resolume (*.xml)|*.xml", "Slices"));
    d.back().enabledIf = "source=xml";
    d.back().tooltip = "Vazio = Documents\\Resolume Arena\\Preferences\\AdvancedOutput.xml";
    d.push_back(makeCustom("reload", "Recarregar slices", "event", Json(0), "Slices"));
    d.back().tooltip = "Relê os slices da saída (ou o XML do Resolume)";

    d.push_back(makeFloat("opacity", "Opacidade", 100.0f, 0.0f, 100.0f, "%.0f %%", "Geral"));
    d.back().tooltip = "Opacidade do resultado sobre a entrada";
    d.push_back(makeBool("blackBg", "Fundo preto", true, "Geral"));
    d.back().tooltip = "Fora dos slices: preto (ligado) ou transparente (desligado)";
    d.push_back(makeFloat("masterSpeed", "Velocidade geral", 1.0f, 0.0f, 2.0f, "%.2fx", "Geral"));
    d.back().tooltip = "Multiplica a velocidade de todos os efeitos, do piscar e do bounce (0 a 2x)";
    d.push_back(makeBool("blinker", "Piscar tudo", false, "Geral"));
    d.back().tooltip = "Força todos os contornos a piscar (bom para mapear)";

    d.push_back(makeBool("remote", "Controle remoto pela rede", false, "Controle remoto"));
    d.back().tooltip = "Sobe o painel web do Slice Refiner neste computador para controlar de tablet ou celular na mesma rede. "
                       "Desligado por padrão: nada fica aberto na rede";
    d.push_back(makeInt("remotePort", "Porta", 8083, 1024, 65535, "%d", "Controle remoto"));
    d.back().tooltip = "Porta HTTP do painel remoto (a do plugin original era 8083). O Windows pode pedir permissão de rede na primeira vez";
    return d;
}

std::unique_ptr<IEffect> makeSliceRefiner3()
{
    return std::make_unique<SliceRefiner3Effect>();
}

} // namespace bsl::fx::bsllight
