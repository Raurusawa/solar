#version 330 core
in vec2 TexCoords;
out float FragColor;

uniform sampler2D gNormal;
uniform sampler2D gDepth;
uniform sampler2D noiseTex;
uniform mat4 invProj;
uniform mat4 proj;
uniform vec2 screenSize;
uniform float uTime;  // 时间抖动，打破噪声平铺滑动

const int KERNEL_SIZE = 16;
uniform vec3 samples[16];  // CPU-generated hemisphere kernel

vec3 viewPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 vp = invProj * ndc;
    return vp.xyz / vp.w;
}

void main() {
    float depth = texture(gDepth, TexCoords).r;
    if (depth >= 0.9999) { FragColor = 1.0; return; }

    vec3 viewPos = viewPosFromDepth(TexCoords, depth);
    vec3 normal = normalize(texture(gNormal, TexCoords).rgb);
    if (length(normal) < 0.1) { FragColor = 1.0; return; }

    // 时间抖动打破 4×4 噪声的滑动伪影
    float jitter = fract(sin(uTime * 12.9898 + TexCoords.x * 78.233) * 43758.5453);
    vec2 noiseUV = TexCoords * screenSize / 4.0 + vec2(jitter, fract(jitter * 1.618));
    vec3 randomVec = normalize(texture(noiseTex, noiseUV).rgb * 2.0 - 1.0);

    // Build TBN
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    float radius = 3.5;       // view-space sample radius
    float bias   = 0.02;

    for (int i = 0; i < KERNEL_SIZE; i++) {
        vec3 sampleOffset = TBN * samples[i];
        vec3 sampleViewPos = viewPos + sampleOffset * radius;

        // Project sample back to screen
        vec4 sampleClip = proj * vec4(sampleViewPos, 1.0);
        vec3 sampleNDC = sampleClip.xyz / sampleClip.w;
        vec2 sampleUV = sampleNDC.xy * 0.5 + 0.5;

        // Skip if out of screen bounds
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = texture(gDepth, sampleUV).r;
        vec3 sampleActualViewPos = viewPosFromDepth(sampleUV, sampleDepth);

        // Compare Z values (both in view space, z is negative in OpenGL)
        float sampleZ = sampleActualViewPos.z;
        float viewZ   = viewPos.z;
        float zdiff  = viewZ - sampleZ;

        // Range check: only consider samples within the radius
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(zdiff + 0.0001));

        // If the sample is closer to camera (higher z), it occludes us
        if (sampleZ > viewZ + bias)
            occlusion += rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    occlusion = pow(occlusion, 1.5);  // sharpen

    FragColor = clamp(occlusion, 0.0, 1.0);
}
