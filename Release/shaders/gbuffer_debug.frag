#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D gColor;

void main() {
    vec3 hdr = texture(gColor, TexCoords).rgb;
    // 输出原始HDR值（clamp到0-1看暗区，但HDR值>1会white）
    // 改用对数映射：log(1+hdr)/log(1+20) 让20=1.0, 1=0.23
    vec3 mapped = log(1.0 + hdr) / log(21.0);
    FragColor = vec4(mapped, 1.0);
}
