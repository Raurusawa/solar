#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec3 ViewNormal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;        // 完整 view 矩阵（含平移），用于 ViewNormal
uniform mat4 viewRot;     // view 矩阵去掉平移部分（仅旋转），用于 gl_Position
uniform mat4 projection;
uniform vec3 cameraPos;   // 相机世界坐标

void main() {
    // model 矩阵的 translate 部分 = worldPos - cameraPos（相机相对坐标，保证 float 精度）
    vec4 relPos = model * vec4(aPos, 1.0);
    // 还原世界坐标给光照计算
    FragPos = vec3(relPos) + cameraPos;
    Normal = mat3(model) * aNormal;
    ViewNormal = normalize(mat3(view) * mat3(model) * aNormal);
    TexCoord = aTexCoord;
    // 用仅含旋转的 viewRot 变换避免 cameraPos 被减两次
    gl_Position = projection * viewRot * relPos;
}
