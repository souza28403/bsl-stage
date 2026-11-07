/*{
    "DESCRIPTION": "Plasma colorido que se move, com duas cores, velocidade, tamanho das manchas e centro.",
    "CREDIT": "Exemplo do BSL Stage (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Generator"],
    "INPUTS": [
        { "NAME": "corA", "LABEL": "Cor A", "TYPE": "color", "DEFAULT": [0.1, 0.2, 0.9, 1.0] },
        { "NAME": "corB", "LABEL": "Cor B", "TYPE": "color", "DEFAULT": [1.0, 0.2, 0.6, 1.0] },
        { "NAME": "velocidade", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0 },
        { "NAME": "escala", "LABEL": "Tamanho das manchas", "TYPE": "float", "DEFAULT": 3.0, "MIN": 0.5, "MAX": 12.0 },
        { "NAME": "centro", "LABEL": "Centro (px em 1920x1080)", "TYPE": "point2D", "DEFAULT": [960.0, 540.0], "MIN": [0.0, 0.0], "MAX": [1920.0, 1080.0] },
        { "NAME": "suave", "LABEL": "Suavizar", "TYPE": "bool", "DEFAULT": true }
    ]
}*/

void main()
{
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 c = centro / vec2(1920.0, 1080.0);
    vec2 p = (isf_FragNormCoord - c) * vec2(aspect, 1.0) * escala;
    float t = TIME * velocidade;
    float v = sin(p.x + t)
            + sin(p.y * 1.3 - t * 0.7)
            + sin((p.x + p.y) * 0.8 + t * 0.5)
            + sin(length(p) * 1.5 - t);
    v = clamp(v * 0.25 + 0.5, 0.0, 1.0);
    if (!suave)
        v = step(0.5, v);
    gl_FragColor = vec4(mix(corA.rgb, corB.rgb, v), 1.0);
}
