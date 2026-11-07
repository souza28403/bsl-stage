/*{
    "DESCRIPTION": "Listras, xadrez, círculos ou pontos em duas cores.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "patternType", "LABEL": "Padrão", "TYPE": "long", "DEFAULT": 0, "VALUES": [0,1,2,3], "LABELS": ["Listras","Xadrez","Círculos","Pontos"] },
        { "NAME": "scale", "LABEL": "Repetições", "TYPE": "float", "DEFAULT": 8, "MIN": 1, "MAX": 40 },
        { "NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 1, "MIN": -5, "MAX": 5 },
        { "NAME": "colorA", "LABEL": "Cor A", "TYPE": "color", "DEFAULT": [1,1,1,1] },
        { "NAME": "colorB", "LABEL": "Cor B", "TYPE": "color", "DEFAULT": [0.05000000074505806,0.05000000074505806,0.20000000298023224,1] }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 p = vec2(uv.x * aspect, uv.y) * scale;
    float t = TIME * speed;
    float m = 0.0;
    if (patternType == 0) {
        m = step(0.5, fract(p.x + p.y * 0.5 - t * 0.25));
    } else if (patternType == 1) {
        vec2 q = floor(p + vec2(t * 0.25, 0.0));
        m = mod(q.x + q.y, 2.0);
    } else if (patternType == 2) {
        float d = length(fract(p) - 0.5);
        m = step(d, 0.25 + 0.15 * sin(t));
    } else {
        float d = length(fract(p + vec2(0.0, t * 0.25)) - 0.5);
        m = 1.0 - smoothstep(0.2, 0.3, d);
    }
    gl_FragColor = vec4(mix(colorB.rgb, colorA.rgb, m), 1.0);
}
