#version 330 core
in vec2 vLocalPos;
out vec4 FragColor;
uniform vec3 uColor;
uniform float uIntensity;

void main() {
    float dist = length(vLocalPos);  // 0=中心, sqrt(2)=角落
    // 紧凑高斯：中心亮、边缘暗
    float glow = exp(-dist * dist * 4.0);
    // 软边缘防硬截断
    float edgeFade = 1.0 - smoothstep(0.6, 1.0, dist);
    glow = max(glow, edgeFade * 0.08);
    FragColor = vec4(uColor * glow * uIntensity, glow);
}
