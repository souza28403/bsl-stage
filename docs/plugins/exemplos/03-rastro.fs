/*{
    "DESCRIPTION": "Rastro luminoso: cada quadro é desenhado sobre o anterior, que vai se apagando. Usa um buffer persistente (passe com memória).",
    "CREDIT": "Exemplo do BSL Stage (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Feedback"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "persistencia", "LABEL": "Persistência", "TYPE": "float", "DEFAULT": 0.9, "MIN": 0.0, "MAX": 0.99 },
        { "NAME": "crescer", "LABEL": "Crescimento do rastro", "TYPE": "float", "DEFAULT": 1.01, "MIN": 0.95, "MAX": 1.08 },
        { "NAME": "limpar", "LABEL": "Limpar rastro", "TYPE": "event" }
    ],
    "PASSES": [
        { "TARGET": "rastro", "PERSISTENT": true, "FLOAT": true },
        { }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    if (PASSINDEX == 0) {
        vec4 src = IMG_THIS_NORM_PIXEL(inputImage);
        vec4 novo = vec4(src.rgb * src.a, src.a);
        vec2 p = (uv - 0.5) / crescer + 0.5;
        vec4 antigo = IMG_NORM_PIXEL(rastro, p) * persistencia;
        vec4 saida = novo + antigo * (1.0 - novo.a);
        if (limpar)
            saida = novo;
        gl_FragColor = saida;
    } else {
        vec4 t = IMG_THIS_NORM_PIXEL(rastro);
        float a = min(t.a, 1.0);
        gl_FragColor = a > 0.0 ? vec4(min(t.rgb / a, vec3(1.0)), a) : vec4(0.0);
    }
}
