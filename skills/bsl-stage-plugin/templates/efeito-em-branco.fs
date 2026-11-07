/*{
    "DESCRIPTION": "Ponto de partida de um efeito: repete a imagem de entrada. Troque o corpo de main().",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);

    vec4 outColor = src;

    gl_FragColor = outColor;
}
