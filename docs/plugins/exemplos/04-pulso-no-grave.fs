/*{
    "DESCRIPTION": "A imagem pulsa e brilha no ritmo dos graves do som (do PC ou do microfone).",
    "CREDIT": "Exemplo do BSL Stage (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "espectro", "LABEL": "Espectro do áudio", "TYPE": "audioFFT" },
        { "NAME": "forca", "LABEL": "Força do pulso", "TYPE": "float", "DEFAULT": 0.5, "MIN": 0.0, "MAX": 1.0 },
        { "NAME": "ganho", "LABEL": "Ganho do áudio", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 4.0 }
    ]
}*/

float faixa(float x)
{
    return IMG_NORM_PIXEL(espectro, vec2(x, 0.5)).r;
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    float grave = (faixa(0.03) + faixa(0.08) + faixa(0.14)) / 3.0 * ganho;
    vec2 p = (uv - 0.5) / (1.0 + grave * forca * 0.15) + 0.5;
    vec4 src = IMG_NORM_PIXEL(inputImage, p);
    gl_FragColor = vec4(src.rgb * (1.0 + grave * forca), src.a);
}
