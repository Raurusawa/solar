#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D sceneTexture;
uniform sampler2D bloomTexture;
uniform sampler2D avgLumTex;     // 1x1, 存 log-average luminance（仅 autoExposure 模式用）
uniform float bloomStrength;
uniform bool  directOutput;
uniform float manualExposure;    // 手动曝光补偿 (-2 ~ +2)，0=自动
uniform bool  autoExposure;      // true=自动曝光, false=固定曝光
uniform float fixedExposure;      // 固定曝光值（autoExposure=false 时使用）
void main() {
    vec3 hdr = texture(sceneTexture, TexCoords).rgb;
    if (directOutput) {
        FragColor = vec4(hdr, 1.0);
        return;
    }
    hdr += texture(bloomTexture, TexCoords).rgb * bloomStrength;

    // --- Exposure ---
    float exposure;
    if (autoExposure) {
        float avgLogLum = texture(avgLumTex, vec2(0.5)).r;
        float avgLum    = exp(avgLogLum);
        float autoExpose = 0.17 / (avgLum + 0.0001);
        exposure = autoExpose * pow(2.0, manualExposure);
    } else {
        exposure = fixedExposure * pow(2.0, manualExposure);
    }

    vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    FragColor = vec4(mapped, 1.0);
}
