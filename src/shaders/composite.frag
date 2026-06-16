#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform sampler2D gColor;
uniform sampler2D ssaoTex;
uniform float uSSAOStrength;
uniform vec2 uSunPos;
uniform float uSunRadius;
uniform float uIntensity;
uniform float uAspectRatio;
uniform vec3 uSunColor;

const int MAX_ECLIPSE_PLANETS = 8;
uniform int uEclipseCount;
uniform vec2 uEclipseCenter[MAX_ECLIPSE_PLANETS];
uniform float uEclipseRadius[MAX_ECLIPSE_PLANETS];

void main() {
    vec3 sceneColor = texture(gColor, TexCoords).rgb;

    // === 1. SSAO ===
    float ssao = texture(ssaoTex, TexCoords).r;
    float aoFactor = mix(1.0, ssao, uSSAOStrength);
    sceneColor *= aoFactor;

    // === 2. 日冕：纯分析型高斯衰减（移除 ray-march 避免十字伪影） ===
    vec2 uv = TexCoords;
    vec2 toSun = uSunPos - uv;
    toSun.x *= uAspectRatio;
    float distToSun = length(toSun);

    // 双层高斯：sigma 非线性缩放 → 近长远短
    // r 小时 bonus 可忽略，r 大时 bonus 主导 → 远距日冕紧贴，近距日冕铺展
    float r = max(uSunRadius, 0.0005);
    float innerSigma = r * (1.0 + r * 15.0);
    float outerSigma = r * (3.5 + r * 50.0);
    float innerGlow = exp(-distToSun * distToSun / (2.0 * innerSigma * innerSigma));
    float outerGlow = exp(-distToSun * distToSun / (2.0 * outerSigma * outerSigma));

    vec3 result = sceneColor;
    result += uSunColor * innerGlow * uIntensity * 0.8;
    result += uSunColor * outerGlow * uIntensity * 0.3;

    // === 3. 日食 ===
    vec2 fragPos = TexCoords * 2.0 - 1.0;
    for (int p = 0; p < uEclipseCount; p++) {
        vec2 toFrag = fragPos - uEclipseCenter[p];
        if (length(toFrag) < uEclipseRadius[p]) {
            float distToEdge = uEclipseRadius[p] - length(toFrag);
            float edgeFrac = distToEdge / max(uEclipseRadius[p], 1e-6);
            float eclipseBlend = smoothstep(0.0, 0.12, edgeFrac);
            result += uSunColor * innerGlow * uIntensity * 0.6 * eclipseBlend;
        }
    }

    FragColor = vec4(result, 1.0);
}
