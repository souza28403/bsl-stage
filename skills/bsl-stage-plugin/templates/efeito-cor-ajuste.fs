/*{
    "DESCRIPTION": "Brilho, contraste, saturação, matiz e tingimento.",
    "CREDIT": "BSL Stage (modelo livre, CC0)",
    "ISFVSN": "2.0",
    "CATEGORIES": ["Meus plugins"],
    "INPUTS": [
        { "NAME": "inputImage", "TYPE": "image" },
        { "NAME": "brightness", "LABEL": "Brilho", "TYPE": "float", "DEFAULT": 0, "MIN": -1, "MAX": 1 },
        { "NAME": "contrast", "LABEL": "Contraste", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 2 },
        { "NAME": "saturation", "LABEL": "Saturação", "TYPE": "float", "DEFAULT": 1, "MIN": 0, "MAX": 2 },
        { "NAME": "hueShift", "LABEL": "Matiz (graus)", "TYPE": "float", "DEFAULT": 0, "MIN": -180, "MAX": 180 },
        { "NAME": "tint", "LABEL": "Cor do tingimento", "TYPE": "color", "DEFAULT": [1,0.6000000238418579,0.20000000298023224,1] },
        { "NAME": "tintAmount", "LABEL": "Quantidade do tingimento", "TYPE": "float", "DEFAULT": 0, "MIN": 0, "MAX": 1 }
    ]
}*/

vec3 rgb2hsv(vec3 c)
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main()
{
    vec2 uv = isf_FragNormCoord;
    vec4 src = IMG_NORM_PIXEL(inputImage, uv);
    vec3 c = src.rgb;

    c += brightness;
    c = (c - 0.5) * contrast + 0.5;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, saturation);
    vec3 hsv = rgb2hsv(clamp(c, 0.0, 1.0));
    hsv.x = fract(hsv.x + hueShift / 360.0);
    c = hsv2rgb(hsv);
    c = mix(c, c * tint.rgb, tintAmount);

    gl_FragColor = vec4(clamp(c, 0.0, 1.0), src.a);
}
