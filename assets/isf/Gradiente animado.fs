/*{
    "DESCRIPTION": "Gradiente suave de três cores que gira e flui com o tempo. Shader de autoria do BSL Stage.",
    "CREDIT": "BSL Stage",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Generator"],
    "INPUTS": [
        { "NAME": "speed",  "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 0.35, "MIN": 0.0, "MAX": 4.0 },
        { "NAME": "scale",  "LABEL": "Escala",     "TYPE": "float", "DEFAULT": 1.2,  "MIN": 0.2, "MAX": 8.0 },
        { "NAME": "angle",  "LABEL": "Ângulo",     "TYPE": "float", "DEFAULT": 35.0, "MIN": -180.0, "MAX": 180.0 },
        { "NAME": "swirl",  "LABEL": "Ondulação",  "TYPE": "float", "DEFAULT": 0.35, "MIN": 0.0, "MAX": 2.0 },
        { "NAME": "colorA", "LABEL": "Cor A", "TYPE": "color", "DEFAULT": [0.93, 0.20, 0.22, 1.0] },
        { "NAME": "colorB", "LABEL": "Cor B", "TYPE": "color", "DEFAULT": [0.10, 0.18, 0.85, 1.0] },
        { "NAME": "colorC", "LABEL": "Cor C", "TYPE": "color", "DEFAULT": [0.98, 0.78, 0.12, 1.0] }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord - 0.5;
    uv.x *= RENDERSIZE.x / RENDERSIZE.y;

    float a = radians(angle);
    vec2 dir = vec2(cos(a), sin(a));
    float t = dot(uv, dir) * scale;
    float side = dot(uv, vec2(-dir.y, dir.x));
    t += swirl * 0.5 * sin(side * 5.0 * scale + TIME * speed * 2.0);
    t += TIME * speed * 0.5;

    float w1 = 0.5 + 0.5 * sin(t * 6.2831853);
    float w2 = 0.5 + 0.5 * sin(t * 3.1415926 + 2.1);
    vec4 col = mix(colorA, colorB, w1);
    col = mix(col, colorC, w2 * 0.8);
    gl_FragColor = vec4(col.rgb, 1.0);
}
