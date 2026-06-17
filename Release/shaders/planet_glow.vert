#version 330 core
layout (location = 0) in vec3 aPos;
out vec2 vLocalPos;
uniform vec2 uCenter;   // NDC 中心
uniform float uSize;    // 光晕 NDC 大小

void main() {
    // aPos.xy in [-1, 1]（复用 quadVAO），缩放到 uSize/2，平移到行星位置
    vec2 pos = uCenter + aPos.xy * uSize * 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
    vLocalPos = aPos.xy;  // [-1, 1]，0=中心
}
