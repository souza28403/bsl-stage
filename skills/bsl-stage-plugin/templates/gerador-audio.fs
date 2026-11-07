/*{
    "DESCRIPTION": "Barras que dançam com o som do PC ou do microfone.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "spectrum", "LABEL": "Espectro do áudio", "TYPE": "audioFFT" },
        { "NAME": "gain", "LABEL": "Ganho do áudio", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 4 },
        { "NAME": "colorLow", "LABEL": "Cor embaixo", "TYPE": "color", "DEFAULT": [0.10000000149011612,0.6000000238418579,1,1] },
        { "NAME": "colorHigh", "LABEL": "Cor em cima", "TYPE": "color", "DEFAULT": [1,0.20000000298023224,0.6000000238418579,1] }
    ]
}*/

float band(float x)
{
    return IMG_NORM_PIXEL(spectrum, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float v = band((floor(uv.x * 32.0) + 0.5) / 32.0) * gain;
    v = max(v, 0.04 + 0.03 * sin(TIME * 2.0 + uv.x * 12.0));
    float cell = fract(uv.x * 32.0);
    float bar = (1.0 - smoothstep(v - 0.01, v + 0.01, uv.y)) * step(0.1, cell) * step(cell, 0.9);
    vec3 bg = vec3(0.05, 0.05, 0.12) + 0.06 * uv.y;
    vec3 col = mix(colorLow.rgb, colorHigh.rgb, uv.y);
    gl_FragColor = vec4(mix(bg, col, bar), 1.0);
}
