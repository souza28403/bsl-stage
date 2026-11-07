/*{
    "DESCRIPTION": "A imagem bate no ritmo dos graves e mostra barras do espectro.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "spectrum", "LABEL": "Espectro do áudio", "TYPE": "audioFFT" },
        { "NAME": "gain", "LABEL": "Ganho do áudio", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 4 },
        { "NAME": "pulse", "LABEL": "Intensidade do pulso", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0, "MAX": 1 },
        { "NAME": "barColor", "LABEL": "Cor das barras", "TYPE": "color", "DEFAULT": [1,0.30000001192092896,0.20000000298023224,1] },
        { "NAME": "showBars", "LABEL": "Mostrar barras", "TYPE": "bool", "DEFAULT": true }
    ]
}*/


float band(float x)
{
    return IMG_NORM_PIXEL(spectrum, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float bass = (band(0.03) + band(0.08) + band(0.14)) / 3.0 * gain;
    vec2 p = (uv - 0.5) / (1.0 + bass * pulse * 0.15) + 0.5;
    vec4 img = IMG_NORM_PIXEL(inputImage, p);
    vec3 c = img.rgb * (1.0 + bass * pulse);
    float a = img.a;
    if (showBars) {
        float v = band(uv.x) * gain * 0.35;
        float cell = fract(uv.x * 32.0);
        float bar = step(uv.y, v) * step(0.1, cell) * step(cell, 0.9);
        c = mix(c, barColor.rgb, bar * 0.85);
        a = max(a, bar * 0.85);
    }
    gl_FragColor = vec4(c, a);
}
