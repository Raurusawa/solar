#version 330 core
in vec3 FragPos;
out vec4 FragColor;
uniform vec3 planetCenter;
uniform float planetRadius;       // 行星半径
uniform float atmosphereRadius;   // 大气外半径 (= planetRadius * 1.15)
uniform vec3 sunDir;              // 指向太阳的单位方向
uniform vec3 cameraPos;

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(FragPos - planetCenter);
    vec3 V = normalize(cameraPos - FragPos);

    // ---- 光学深度：射线在大气层里的距离 ----
    vec3 rayOrigin = cameraPos;
    vec3 rayDir    = normalize(FragPos - cameraPos);
    float tFar     = length(FragPos - cameraPos);

    // 大气外层球体求交
    vec3  oc    = rayOrigin - planetCenter;
    float a     = dot(rayDir, rayDir);
    float b     = 2.0 * dot(oc, rayDir);
    float c_out = dot(oc, oc) - atmosphereRadius * atmosphereRadius;
    float disc  = b * b - 4.0 * a * c_out;
    if (disc <= 0.0) { FragColor = vec4(0.0); return; }

    float sqrtDisc = sqrt(disc);
    float tEnter = max(0.0, (-b - sqrtDisc) / (2.0 * a));
    float tExit  = (-b + sqrtDisc) / (2.0 * a);
    tExit = min(tExit, tFar);

    // 射线可能击中行星 → 截断大气深度
    float c_in = dot(oc, oc) - planetRadius * planetRadius;
    float discIn = b * b - 4.0 * a * c_in;
    if (discIn > 0.0) {
        float tPlanet = (-b - sqrt(discIn)) / (2.0 * a);
        if (tPlanet > tEnter && tPlanet < tExit) tExit = tPlanet;
    }

    float opticalDepth = (tExit - tEnter) / (atmosphereRadius - planetRadius + 0.001);
    opticalDepth = clamp(opticalDepth, 0.0, 1.0);

    // ---- 大气散射 ----
    float mu  = dot(N, V);
    float muS = dot(N, sunDir);

    // ---- 行星遮挡：大气层中的点在行星阴影内则不被照亮 ----
    // 从大气点向太阳方向发射射线，若与行星相交则处于阴影中
    vec3 toPlanetCenter = planetCenter - FragPos;
    float tProj = dot(toPlanetCenter, sunDir);
    float dSq   = dot(toPlanetCenter, toPlanetCenter) - tProj * tProj;

    // dRatio: 0=射线穿过行星中心, 1=擦边, >1=完全在阴影锥外
    float dRatio = dSq / (planetRadius * planetRadius + 0.001);

    // 连续阴影因子，无硬边界：完全阴影(dRatio<0.5) → 完全照亮(dRatio>1.1)
    float inShadow = 0.0;
    if (tProj > 0.0) {
        inShadow = 1.0 - smoothstep(0.5, 1.1, dRatio);
    }

    // Rayleigh: 蓝光散射，越近边缘越强
    float rayleighPhase = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
    float rayleigh      = opticalDepth * rayleighPhase * (1.0 - abs(mu));

    // Mie: 前向散射 (Henyey-Greenstein, g=0.76)
    float g   = 0.76;
    float cosTheta = dot(normalize(sunDir), -V);  // 视线与阳光夹角
    float miePhase  = (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5));
    float mie        = opticalDepth * miePhase;

    vec3 rayleighColor = vec3(0.25, 0.55, 1.0)  * rayleigh * 0.7;
    vec3 mieColor      = vec3(1.0,  0.90, 0.65) * mie      * 0.35;

    vec3 color = (rayleighColor + mieColor) * (1.0 - inShadow);

    float alpha = opticalDepth * 0.6 + mie * 0.2;
    alpha = clamp(alpha, 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
