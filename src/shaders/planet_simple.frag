#version 330 core
out vec4 FragColor;
uniform vec3 color; // 从外部传入纯色
void main() {
    FragColor = vec4(color, 1.0);
}
