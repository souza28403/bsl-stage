/*{
    "DESCRIPTION": "Grade de teste para painel de LED: células coloridas, linhas, moldura, diagonais, marcas de origem e uma barra que varre a tela. Shader de autoria do BSL Stage.",
    "CREDIT": "BSL Stage",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Generator", "Utility"],
    "INPUTS": [
        { "NAME": "columns", "LABEL": "Colunas", "TYPE": "float", "DEFAULT": 32.0, "MIN": 4.0, "MAX": 256.0 },
        { "NAME": "pattern", "LABEL": "Padrão", "TYPE": "long", "DEFAULT": 0,
          "VALUES": [0, 1, 2, 3], "LABELS": ["Cores por célula", "Branco", "Rampa de cinza", "Xadrez"] },
        { "NAME": "lineWidth", "LABEL": "Espessura da linha", "TYPE": "float", "DEFAULT": 0.06, "MIN": 0.0, "MAX": 0.5 },
        { "NAME": "frame", "LABEL": "Moldura e diagonais", "TYPE": "bool", "DEFAULT": true },
        { "NAME": "scan", "LABEL": "Barra de varredura", "TYPE": "bool", "DEFAULT": true },
        { "NAME": "tint", "LABEL": "Cor das linhas", "TYPE": "color", "DEFAULT": [1.0, 1.0, 1.0, 1.0] }
    ]
}*/

vec3 hue(float h)
{
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return k;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float cell = RENDERSIZE.x / max(columns, 1.0);                      // lado da célula em pixels
    vec2 px = vec2(uv.x * RENDERSIZE.x, (1.0 - uv.y) * RENDERSIZE.y);   // pixels, origem no canto superior esquerdo
    vec2 id = floor(px / cell);
    vec2 f = fract(px / cell);

    vec3 col;
    if (pattern == 0) {
        col = 0.12 + 0.55 * hue((id.x + id.y * 3.0) / max(columns, 1.0));
    } else if (pattern == 1) {
        col = vec3(0.85);
    } else if (pattern == 2) {
        col = vec3(uv.x);
    } else {
        col = vec3(mod(id.x + id.y, 2.0) < 1.0 ? 0.9 : 0.1);
    }

    // linhas da grade
    vec2 d = min(f, 1.0 - f);
    float line = 1.0 - smoothstep(lineWidth * 0.5, lineWidth * 0.5 + 0.04, min(d.x, d.y));
    col = mix(col, tint.rgb, line * tint.a);

    if (frame) {
        // moldura e diagonais do canvas
        vec2 e = min(px, RENDERSIZE - px);
        float border = 1.0 - smoothstep(cell * 0.45, cell * 0.45 + 1.0, min(e.x, e.y));
        col = mix(col, vec3(1.0, 0.1, 0.1), border);
        float diag = min(abs(px.y / RENDERSIZE.y - px.x / RENDERSIZE.x),
                         abs(px.y / RENDERSIZE.y - (1.0 - px.x / RENDERSIZE.x))) * RENDERSIZE.y;
        col = mix(col, vec3(1.0), 1.0 - smoothstep(cell * 0.12, cell * 0.12 + 1.0, diag));
        // marcas de origem: célula do canto superior esquerdo em ciano, a do canto superior direito em amarelo
        if (id == vec2(0.0))
            col = vec3(0.0, 0.9, 0.9);
        if (id.x == floor(columns) - 1.0 && id.y == 0.0)
            col = vec3(0.95, 0.85, 0.1);
    }

    if (scan) {
        float pos = fract(TIME * 0.25);
        col += vec3(0.5) * (1.0 - smoothstep(0.0, 0.015, abs(uv.x - pos)));
    }
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
