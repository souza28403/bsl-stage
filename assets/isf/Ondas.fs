/*{
    "DESCRIPTION": "Distorce a imagem com ondas senoidais (horizontais, verticais ou circulares a partir de um ponto). Shader de autoria do BSL Stage.",
    "CREDIT": "BSL Stage",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Distortion Effect"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "mode", "LABEL": "Direção", "TYPE": "long", "DEFAULT": 0,
          "VALUES": [0, 1, 2], "LABELS": ["Horizontal", "Vertical", "Circular"] },
        { "NAME": "amount", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 0.02, "MIN": 0.0, "MAX": 0.2 },
        { "NAME": "frequency", "LABEL": "Frequência", "TYPE": "float", "DEFAULT": 8.0, "MIN": 0.5, "MAX": 60.0 },
        { "NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 2.0, "MIN": -10.0, "MAX": 10.0 },
        { "NAME": "origin", "LABEL": "Origem (px em 1920x1080)", "TYPE": "point2D", "DEFAULT": [960.0, 540.0],
          "MIN": [0.0, 0.0], "MAX": [1920.0, 1080.0] }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    float phase = TIME * speed;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 offset = vec2(0.0);

    if (mode == 0) {
        offset.x = amount * sin(uv.y * frequency * 6.2831853 + phase);
    } else if (mode == 1) {
        offset.y = amount * sin(uv.x * frequency * 6.2831853 + phase);
    } else {
        vec2 c = origin / vec2(1920.0, 1080.0);
        vec2 d = uv - c;
        d.x *= aspect;
        float r = length(d);
        vec2 dirn = r > 0.0001 ? d / r : vec2(0.0);
        dirn.x /= aspect;
        offset = dirn * amount * sin(r * frequency * 6.2831853 - phase);
    }
    gl_FragColor = IMG_NORM_PIXEL(inputImage, uv + offset);
}
