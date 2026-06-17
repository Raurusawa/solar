#version 330 core
layout(location = 0) out vec4 gColor;
layout(location = 1) out vec4 gNormal;

in vec3 FragPos;
in vec3 Normal;
in vec3 ViewNormal;
in vec2 TexCoord;

uniform sampler2D textureSampler;
uniform vec3  viewPos;
uniform float roughness;
uniform float metallic;
uniform float lightIntensity;
uniform float sunRadius;       // 太阳世界空间半径
uniform bool  hasAtmosphere;
uniform vec3  uBaseColor;      // 基础颜色倍增 (行星=vec3(1.0), 卫星=实际颜色)
uniform float uShadowFactor;   // 行星遮挡投影 (1.0=全亮, 0.0=全影)

const float PI = 3.14159265359;
const vec3  sunCenter = vec3(0.0);

// ========== PBR GGX ==========
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float a) {
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 0.0001);
}

float GeometrySchlickGGX(float NdotV, float k) {
    return NdotV / (NdotV * (1.0 - k) + k + 0.0001);
}

float GeometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return GeometrySchlickGGX(NdotV, k) * GeometrySchlickGGX(NdotL, k);
}

void main() {
    vec3 albedo = texture(textureSampler, TexCoord).rgb * uBaseColor;

    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);

    // 太阳在原点
    vec3 L = normalize(sunCenter - FragPos);
    float distToSun = length(sunCenter - FragPos);

    // 太阳角半径（弧度）
    float angularRadius = atan(sunRadius / distToSun);

    // Angular radius soft shadow (Space Engine scheme)
    // Penumbra width = 2 × sun angular radius, physically correct
    float NdotL_center = dot(N, L);  // allow negative for penumbra calc
    float NdotL = smoothstep(-angularRadius, angularRadius, NdotL_center);

    // 距离衰减：1/dist 线性
    float attenuation = lightIntensity / max(distToSun, 0.1);

    // 环境光：关闭
    float ambientFactor = 0.0;
    vec3 ambient = vec3(0.0);

    // 高光 representative point 近似
    vec3 H;
    if (NdotL_center > 0.0) {
        vec3 R = reflect(-L, N);
        vec3 repPoint = sunCenter + normalize(R) * sunRadius;
        vec3 Lrep = normalize(repPoint - FragPos);
        H = normalize(V + Lrep);
    } else {
        H = normalize(V + L);
    }

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // F0
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Fresnel
    vec3 F = fresnelSchlick(max(VdotH, 0.0), F0);

    // GGX NDF（面积粗糙度）
    float rough2 = roughness * roughness;
    float areaRoughness = rough2 + angularRadius * 0.5;
    float D = DistributionGGX(N, H, areaRoughness);

    // Geometry
    float G = GeometrySmith(NdotV, max(NdotL, 0.0), roughness);

    // Specular
    vec3 specular = (D * G * F) / (4.0 * max(NdotV, 0.0) * max(NdotL, 0.0) + 0.0001);

    // Diffuse
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // 组合
    vec3 radiance = attenuation * (diffuse + specular) * NdotL;
    vec3 color = ambient + radiance;

    gColor = vec4(color * uShadowFactor, 1.0);
    gNormal = vec4(normalize(ViewNormal), 0.0);
}
