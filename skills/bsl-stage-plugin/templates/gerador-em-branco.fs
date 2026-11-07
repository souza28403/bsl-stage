/*{
    "DESCRIPTION": "Ponto de partida de um gerador: degradê animado. Troque o corpo de main().",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;

    vec3 color = vec3(uv.x, uv.y, 0.5 + 0.5 * sin(TIME));

    gl_FragColor = vec4(color, 1.0);
}
