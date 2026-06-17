#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 mvp;   // 模型视图投影矩阵合并传入
void main() {
    gl_Position = mvp * vec4(aPos, 1.0);
}
