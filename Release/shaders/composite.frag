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
uniform float uSunDiscVisible;

const int MAX_ECLIPSE_PLANETS = 32;
uniform int uEclipseCount;
uniform vec2 uEclipseCenter[MAX_ECLIPSE_PLANETS];
uniform float uEclipseRadius[MAX_ECLIPSE_PLANETS];
uniform bool uEclipseRimGlow[MAX_ECLIPSE_PLANETS];

void main() {
    vec3 sceneColor = texture(gColor, TexCoords).rgb;

    // === 1. SSAO ===
    float sunDist = length((uSunPos - TexCoords) * vec2(uAspectRatio, 1.0));
    float inSunDisc = 1.0 - smoothstep(uSunRadius * 0.8, uSunRadius * 1.05, sunDist);
    float ssao = texture(ssaoTex, TexCoords).r;
    float aoFactor = mix(1.0, ssao, uSSAOStrength);
    sceneColor *= mix(aoFactor, 1.0, inSunDisc);

    // === 2. 行星 proximity：离行星越近，corona 扩散越弱 ===
    //     用屏上最大行星半径近似距离（越大 = 越近）
    float maxPlanetR = 0.0;
    for (int p = 0; p < uEclipseCount; p++) {
        maxPlanetR = max(maxPlanetR, uEclipseRadius[p]);
    }
    // smoothstep: far(close to 0) → 1.0, near(>0.35) → 0.0
    float proximity = 1.0 - smoothstep(0.05, 0.35, maxPlanetR);

    // === 3. corona: analytic double Gaussian ===
    vec2 uv = TexCoords;
    vec2 toSun = uSunPos - uv;
    toSun.x *= uAspectRatio;
    float distToSun = length(toSun);

    float r = max(uSunRadius, 0.0005);
    // sigma 随 proximity 缩放：离行星近时扩散范围收窄
    float sigmaScale = mix(0.15, 1.0, proximity);
    float innerSigma = r * (1.0 + r * 15.0) * sigmaScale;
    float outerSigma = r * (3.5 + r * 50.0) * sigmaScale;
    float innerGlow = exp(-distToSun * distToSun / (2.0 * innerSigma * innerSigma));
    float outerGlow = exp(-distToSun * distToSun / (2.0 * outerSigma * outerSigma));

    // === 4. 遮挡检测 ===
    vec2 fragNDC = TexCoords * 2.0 - 1.0;
    vec2 sunNDC  = uSunPos * 2.0 - 1.0;

    // 4a. geoOcclude: 像素在行星 disc 内（精确半径）
    float geoOcclude = 0.0;
    for (int p = 0; p < uEclipseCount; p++) {
        vec2 d = fragNDC - uEclipseCenter[p];
        d.x *= uAspectRatio;
        float distPC = length(d);
        float edgePix = fwidth(distPC);
        float soft = 1.0 - smoothstep(uEclipseRadius[p] - edgePix,
                                        uEclipseRadius[p] + edgePix, distPC);
        geoOcclude = max(geoOcclude, soft);
    }

    // 4b. sunGeomOccluded: 太阳中心被行星遮挡 → corona 全局衰减
    //     连续过渡（非二值阶跃），避免视角微动时突变
    float sunR_NDC = uSunRadius * 2.0;
    float sunGeomOccluded = 0.0;
    for (int p = 0; p < uEclipseCount; p++) {
        vec2 d = sunNDC - uEclipseCenter[p];
        d.x *= uAspectRatio;
        float dist = length(d);
        float threshold = uEclipseRadius[p] + sunR_NDC;
        float fadeWidth = sunR_NDC * 2.0;
        float occ = 1.0 - smoothstep(threshold - fadeWidth, threshold + fadeWidth, dist);
        sunGeomOccluded = max(sunGeomOccluded, occ);
    }

    float coronaOcclude = max(geoOcclude, sunGeomOccluded);

    // === 5. 合成 ===
    vec3 result = sceneColor;
    float coronaVis = (1.0 - coronaOcclude) * uSunDiscVisible;
    // TODO: 暂时关闭高斯扩散
    // result += uSunColor * innerGlow * uIntensity * 0.8 * coronaVis;
    // result += uSunColor * outerGlow * uIntensity * 0.3 * coronaVis;

    // === 6. eclipse rim glow ===
    for (int p = 0; p < uEclipseCount; p++) {
        if (!uEclipseRimGlow[p]) continue;
        vec2 d = fragNDC - uEclipseCenter[p];
        d.x *= uAspectRatio;
        float distToPlanetCenter = length(d);
        float planetR = uEclipseRadius[p];
        float distToEdge = planetR - distToPlanetCenter;
        float edgeFrac = distToEdge / max(planetR, 1e-6);
        float ringMask = smoothstep(0.0, 0.08, edgeFrac)
                      * (1.0 - smoothstep(0.08, 0.12, edgeFrac));
        float eclipseBlend = ringMask;
        result += uSunColor * innerGlow * uIntensity * 0.6
                * eclipseBlend * coronaVis;
    }

    FragColor = vec4(result, 1.0);
}
