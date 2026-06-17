#version 330 core
layout(location = 0) in vec3 aPos;
out vec3 FragPos;
uniform mat4 model;
uniform mat4 viewRot;     // view 矩阵去掉平移部分（仅旋转）
uniform mat4 projection;
uniform vec3 cameraPos;   // 相机世界坐标，还原 FragPos 到世界空间
void main() {
    // model 的 translate = planetCenter - cameraPos（相对坐标，float 精度安全）
    vec4 relPos = model * vec4(aPos, 1.0);
    FragPos = relPos.xyz + cameraPos;
    gl_Position = projection * viewRot * relPos;
}
