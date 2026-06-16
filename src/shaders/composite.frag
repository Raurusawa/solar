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

    // === 2. geometric planet occlusion ===
    vec2 fragNDC = TexCoords * 2.0 - 1.0;
    float geoOcclude = 0.0;  // 0 = visible, 1 = fully blocked
    for (int p = 0; p < uEclipseCount; p++) {
        vec2 d = fragNDC - uEclipseCenter[p];
        d.x *= uAspectRatio;  // 修正NDC宽高比：世界空间圆→NDC椭圆，反向补偿
        float distToPlanetCenter = length(d);
        float planetR = uEclipseRadius[p];
        if (distToPlanetCenter < planetR) {
            // inside planet disc → fully occlude corona
            geoOcclude = 1.0;
            break;
        }
    }

    // === 3. corona: analytic double Gaussian ===
    vec2 uv = TexCoords;
    vec2 toSun = uSunPos - uv;
    toSun.x *= uAspectRatio;
    float distToSun = length(toSun);

    float r = max(uSunRadius, 0.0005);
    float innerSigma = r * (1.0 + r * 15.0);
    float outerSigma = r * (3.5 + r * 50.0);
    float innerGlow = exp(-distToSun * distToSun / (2.0 * innerSigma * innerSigma));
    float outerGlow = exp(-distToSun * distToSun / (2.0 * outerSigma * outerSigma));

    vec3 result = sceneColor;
    float coronaVis = 1.0 - geoOcclude;  // blocked by planet disc
    result += uSunColor * innerGlow * uIntensity * 0.8 * coronaVis;
    result += uSunColor * outerGlow * uIntensity * 0.3 * coronaVis;

    // === 4. eclipse rim glow ===
    // only on planet edges that touch the sun
    for (int p = 0; p < uEclipseCount; p++) {
        vec2 d = fragNDC - uEclipseCenter[p];
        d.x *= uAspectRatio;  // 修正NDC宽高比
        float distToPlanetCenter = length(d);
        float planetR = uEclipseRadius[p];
        if (distToPlanetCenter < planetR) {
            float distToEdge = planetR - distToPlanetCenter;
            float edgeFrac = distToEdge / max(planetR, 1e-6);
            float eclipseBlend = smoothstep(0.0, 0.12, edgeFrac);
            result += uSunColor * innerGlow * uIntensity * 0.6 * eclipseBlend * coronaVis;
        }
    }

    FragColor = vec4(result, 1.0);
}
