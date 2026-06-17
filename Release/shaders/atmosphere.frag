#version 330 core
in vec3 FragPos;
out vec4 FragColor;
uniform vec3 planetCenter;
uniform float planetRadius;
uniform float atmosphereRadius;
uniform vec3 sunDir;
uniform vec3 cameraPos;

// Per-planet atmosphere parameters (set from main.cpp)
uniform vec3 uRayleighColor;   // Rayleigh scattering color
uniform vec3 uMieColor;       // Mie scattering color
uniform float uDensityFalloff; // density exponent: higher = thinner atmosphere
uniform float uRayleighStrength;
uniform float uMieStrength;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(FragPos - planetCenter);
    vec3 V = normalize(cameraPos - FragPos);

    // ---- altitude-based density falloff (exponential) ----
    float altitude = length(FragPos - planetCenter) - planetRadius;
    float atmThickness = atmosphereRadius - planetRadius;
    float hNorm = clamp(altitude / max(atmThickness, 0.001), 0.0, 1.0);
    // exponential density: dense at surface, thin at top
    float density = exp(-hNorm * uDensityFalloff);
    // Screen-space limb fade: wide gradient at disc edge (~23°) → no hard boundary
    float edgeFade = smoothstep(0.0, 0.4, abs(dot(N, V)));

    // ---- optical depth along ray ----
    vec3 rayOrigin = cameraPos;
    vec3 rayDir    = normalize(FragPos - cameraPos);
    float tFar     = length(FragPos - cameraPos);

    vec3  oc    = rayOrigin - planetCenter;
    float a     = dot(rayDir, rayDir);
    float b     = 2.0 * dot(oc, rayDir);
    float c_out = dot(oc, oc) - atmosphereRadius * atmosphereRadius;
    float disc  = b * b - 4.0 * a * c_out;

    // camera inside atmosphere? → start at ray origin, not at shell entry
    bool cameraInsideAtmo = dot(oc, oc) < atmosphereRadius * atmosphereRadius;

    float tEnter, tExit;
    if (cameraInsideAtmo) {
        tEnter = 0.0;
        tExit  = tFar;
    } else {
        if (disc <= 0.0) { FragColor = vec4(0.0); return; }
        float sqrtDisc = sqrt(disc);
        tEnter = max(0.0, (-b - sqrtDisc) / (2.0 * a));
        tExit  = (-b + sqrtDisc) / (2.0 * a);
        tExit  = min(tExit, tFar);
    }

    float c_in = dot(oc, oc) - planetRadius * planetRadius;
    float discIn = b * b - 4.0 * a * c_in;
    if (discIn > 0.0) {
        float tPlanet = (-b - sqrt(discIn)) / (2.0 * a);
        if (tPlanet > tEnter && tPlanet < tExit) tExit = tPlanet;
    }

    float opticalDepth = (tExit - tEnter) / max(atmThickness, 0.001);
    opticalDepth = clamp(opticalDepth, 0.0, 1.0);

    // ---- scattering ----
    float mu  = dot(N, V);
    float muS = dot(N, sunDir);

    vec3 toPlanetCenter = planetCenter - FragPos;
    float tProj = dot(toPlanetCenter, sunDir);
    float dSq   = dot(toPlanetCenter, toPlanetCenter) - tProj * tProj;
    float dRatio = dSq / (planetRadius * planetRadius + 0.001);
    float inShadow = 0.0;
    if (tProj > 0.0) {
        inShadow = 1.0 - smoothstep(0.5, 1.1, dRatio);
    }

    // Rayleigh
    float rayleighPhase = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float rayleigh      = opticalDepth * rayleighPhase * (1.0 - abs(mu));

    // Mie
    float g   = 0.76;
    float cosTheta = dot(normalize(sunDir), -V);
    float miePhase  = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5));
    float mie        = opticalDepth * miePhase;

    vec3 rayleighColor = uRayleighColor * rayleigh * uRayleighStrength * density;
    vec3 mieColor      = uMieColor      * mie      * uMieStrength      * density;

    // edgeFade applied to color (GL_ONE/GL_ONE ignores alpha, so fade must be in RGB)
    vec3 color = (rayleighColor + mieColor) * (1.0 - inShadow) * edgeFade;

    float alpha = (opticalDepth * 0.6 + mie * 0.2) * edgeFade;
    alpha = clamp(alpha, 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
