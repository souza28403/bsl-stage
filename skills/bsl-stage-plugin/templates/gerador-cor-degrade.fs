/*{
    "DESCRIPTION": "Degradê entre duas cores, com ângulo e movimento.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "colorA", "LABEL": "Cor A", "TYPE": "color", "DEFAULT": [0.10000000149011612,0.4000000059604645,1,1] },
        { "NAME": "colorB", "LABEL": "Cor B", "TYPE": "color", "DEFAULT": [1,0.20000000298023224,0.5,1] },
        { "NAME": "angle", "LABEL": "Ângulo (graus)", "TYPE": "float", "DEFAULT": 0, "MIN": -180, "MAX": 180 },
        { "NAME": "speed", "LABEL": "Velocidade", "TYPE": "float", "DEFAULT": 0.3, "MIN": -2, "MAX": 2 }
    ]
}*/

void main()
{
    vec2 uv = isf_FragNormCoord;
    float a = radians(angle);

    float t = dot(uv - 0.5, vec2(cos(a), sin(a))) + 0.5;
    t += 0.15 * sin(TIME * speed * 3.0);
    vec3 color = mix(colorA.rgb, colorB.rgb, clamp(t, 0.0, 1.0));
    gl_FragColor = vec4(color, 1.0);
}
