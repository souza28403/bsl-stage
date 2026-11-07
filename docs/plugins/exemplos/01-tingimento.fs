/*{
    "DESCRIPTION": "Tinge a imagem com uma cor, mantendo a luz e a sombra originais.",
    "CREDIT": "Exemplo do BSL Stage (CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Color Effect"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "cor", "LABEL": "Cor do tingimento", "TYPE": "color", "DEFAULT": [1.0, 0.45, 0.1, 1.0] },
        { "NAME": "quantidade", "LABEL": "Quantidade", "TYPE": "float", "DEFAULT": 0.6, "MIN": 0.0, "MAX": 1.0 }
    ]
}*/

void main()
{
    vec4 src = IMG_THIS_NORM_PIXEL(inputImage);
    float luz = dot(src.rgb, vec3(0.2126, 0.7152, 0.0722));
    vec3 tingido = cor.rgb * luz * 1.5;
    gl_FragColor = vec4(mix(src.rgb, tingido, quantidade), src.a);
}
