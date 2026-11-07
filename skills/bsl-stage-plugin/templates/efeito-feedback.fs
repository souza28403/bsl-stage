/*{
    "DESCRIPTION": "Rastro que fica na tela, gira e cresce (passes persistentes).",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "decay", "LABEL": "Persistência", "TYPE": "float", "DEFAULT": 0.92, "MIN": 0, "MAX": 0.995 },
        { "NAME": "zoom", "LABEL": "Zoom do rastro", "TYPE": "float", "DEFAULT": 1.01, "MIN": 0.9, "MAX": 1.1 },
        { "NAME": "rotate", "LABEL": "Rotação do rastro (graus)", "TYPE": "float", "DEFAULT": 0.5, "MIN": -10, "MAX": 10 },
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
        vec4 src = IMG_THIS_NORM_PIXEL(inputImage);
        vec4 cur = vec4(src.rgb * src.a, src.a);
        float a = radians(rotate);
        float aspect = RENDERSIZE.x / RENDERSIZE.y;
        vec2 p = (uv - 0.5) * vec2(aspect, 1.0);
        p = vec2(cos(a) * p.x - sin(a) * p.y, sin(a) * p.x + cos(a) * p.y) / zoom;
        p = p / vec2(aspect, 1.0) + 0.5;
        vec4 prev = IMG_NORM_PIXEL(trail, p) * decay;
        vec4 o = cur + prev * (1.0 - cur.a);
        if (reset)
            o = cur;
        gl_FragColor = o;
    } else {
        vec4 t = IMG_THIS_NORM_PIXEL(trail);
        float al = min(t.a, 1.0);
        gl_FragColor = al > 0.0 ? vec4(min(t.rgb / al, vec3(1.0)), al) : vec4(0.0);
    }
}
