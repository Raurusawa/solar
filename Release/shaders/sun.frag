#version 330 core
layout(location = 0) out vec4 gColor;
layout(location = 1) out vec4 gNormal;

in vec3 FragPos;
in vec3 Normal;
in vec3 ViewNormal;

uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 viewPos;

void main() {
    // 太阳每个面独立自发光
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);
    float mu = max(dot(N, V), 0.0);

    // Limb darkening (quadratic law): 太阳边缘暗、中心亮
    // 典型可见光系数 u=0.50, v=0.30
    float u = 0.50;
    float v = 0.30;
    float limbFactor = 1.0 - u * (1.0 - mu) - v * (1.0 - mu) * (1.0 - mu);
    limbFactor = max(limbFactor, 0.15);  // 边缘不完全黑

    // 边缘略微偏红 (物理：边缘色温更低)
    vec3 limbColor = mix(vec3(1.0, 0.6, 0.2), vec3(1.0, 0.95, 0.8), limbFactor);

    float emission = uSunIntensity * limbFactor;

    gNormal = vec4(normalize(ViewNormal), 0.0);
    gColor = vec4(uSunColor * limbColor * emission, 1.0);
}
