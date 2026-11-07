/*{
    "DESCRIPTION": "Ondas que deformam a imagem a partir de um ponto.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "amount", "LABEL": "Intensidade", "TYPE": "float", "DEFAULT": 0.03, "MIN": 0, "MAX": 0.2 },
        { "NAME": "frequency", "LABEL": "Frequência", "TYPE": "float", "DEFAULT": 6, "MIN": 0.5, "MAX": 40 },
        { "NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 1, "MIN": -5, "MAX": 5 },
        { "NAME": "center", "LABEL": "Centro (px em 1920x1080)", "TYPE": "point2D", "DEFAULT": [960,540], "MIN": [0,0], "MAX": [1920,1080] }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    float aspect = RENDERSIZE.x / RENDERSIZE.y;
    vec2 c = center / vec2(1920.0, 1080.0);
    vec2 d = (uv - c) * vec2(aspect, 1.0);
    float r = length(d);
    float wave = sin(r * frequency * 6.2831853 - TIME * speed * 2.0);
    vec2 dir = r > 0.0001 ? d / r : vec2(0.0);
    vec2 offset = dir * wave * amount / vec2(aspect, 1.0);
    gl_FragColor = IMG_NORM_PIXEL(inputImage, uv + offset);
}
