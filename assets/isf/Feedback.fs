/*{
    "DESCRIPTION": "Rastro de feedback: cada quadro é desenhado sobre o anterior, que vai se apagando e pode girar e crescer. Usa um buffer persistente. Shader de autoria do BSL Stage.",
    "CREDIT": "BSL Stage",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Feedback", "Stylize"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "decay", "LABEL": "Persistência", "TYPE": "float", "DEFAULT": 0.9, "MIN": 0.0, "MAX": 0.995 },
        { "NAME": "zoom", "LABEL": "Zoom do rastro", "TYPE": "float", "DEFAULT": 1.01, "MIN": 0.9, "MAX": 1.1 },
        { "NAME": "rotate", "LABEL": "Rotação do rastro (graus)", "TYPE": "float", "DEFAULT": 0.5, "MIN": -10.0, "MAX": 10.0 },
        { "NAME": "reset", "LABEL": "Limpar rastro", "TYPE": "event" }
    ],
    "PASSES": [
        { "TARGET": "trail", "PERSISTENT": true, "FLOAT": true },
        { }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    if (PASSINDEX == 0) {
        vec4 cur = IMG_THIS_NORM_PIXEL(inputImage);
        float a = radians(rotate);
        float c = cos(a);
        float s = sin(a);
        float aspect = RENDERSIZE.x / RENDERSIZE.y;
        vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
        p = vec2(c * p.x - s * p.y, s * p.x + c * p.y) / zoom;
        p = p / vec2(aspect, 1.0) + 0.5;
        vec4 prev = IMG_NORM_PIXEL(trail, p) * decay;
        vec4 o = cur + prev * (1.0 - cur.a);
        if (reset)
            o = cur;
        gl_FragColor = o;
    } else {
        gl_FragColor = IMG_THIS_NORM_PIXEL(trail);
    }
}
