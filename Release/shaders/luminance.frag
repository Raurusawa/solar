#version 330 core
layout(location = 0) out float fragColor;
in vec2 TexCoords;
uniform sampler2D inputTex;
uniform vec2  inputRes;   // 输入纹理分辨率
uniform bool  firstPass;    // 第一遍需要计算 log(luminance)

void main() {
    // TexCoords 来自全屏 quad，覆盖 [0,1]×[0,1]
    // 当下采样 2x 时，每个输出像素对应输入 2×2 块
    vec2 texelSize = 1.0 / inputRes;
    float sum = 0.0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            vec2 offset = texelSize * vec2(float(i) - 0.5, float(j) - 0.5);
            vec2 coord  = TexCoords + offset;
            float val;
            if (firstPass) {
                vec3 col  = texture(inputTex, coord).rgb;
                float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
                val = log(lum + 1e-5);
            } else {
                val = texture(inputTex, coord).r;
            }
            sum += val;
        }
    }
    fragColor = sum / 4.0;
}
