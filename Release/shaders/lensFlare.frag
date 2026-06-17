#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform vec2  uSunPos;
uniform float uSunVisible;
uniform float uSunRadius;
uniform float uIntensity;
uniform float uAspectRatio;

const int MAX_ECLIPSE_PLANETS = 32;
uniform int   uEclipseCount;
uniform vec2  uEclipseCenter[MAX_ECLIPSE_PLANETS];
uniform float uEclipseRadius[MAX_ECLIPSE_PLANETS];

uniform sampler2D uSunEmissive;

const int N_EMITTERS = 16;

void main() {
    vec2 uv = TexCoords;
    vec3 flare = vec3(0.0);
    int  emitterCount = 0;
    vec3 emissiveSum = vec3(0.0);

    float r = max(uSunRadius, 0.0001);
    for (int s = 0; s < N_EMITTERS; s++) {
        float phi   = 2.399963 * float(s);
        float rFrac = sqrt((float(s) + 0.5) / float(N_EMITTERS));
        vec2  sampleUV = uSunPos + r * vec2(cos(phi), sin(phi)) * rFrac;

        vec3 emissive = texture(uSunEmissive, sampleUV).rgb;
        float em = max(max(emissive.r, emissive.g), emissive.b);
        if (em < 0.01) continue;

        emitterCount++;
        emissiveSum += emissive;

        vec2 toSample = uv - sampleUV;
        toSample.x *= uAspectRatio;
        float d  = length(toSample);
        float nd = d / r;
        float glow = 1.0 / (1.0 + nd * nd * 1.5);
        vec3 glowCol = mix(vec3(1.0, 0.28, 0.04), vec3(1.0, 0.92, 0.60),
                           1.0 / (1.0 + nd * 0.7));
        flare += glowCol * glow * 1.1 * em;

        float pointness = 1.0 / (1.0 + r / 0.02);
        float streakE  = pointness * 0.5;
        vec2  dUV = uv - sampleUV;
        float dx  = dUV.x * uAspectRatio;
        float dy  = dUV.y;
        float shThin = max(r * 0.38, 0.0005);
        float shLen  = max(r * 6.0, 0.003);
        float h0 = exp(-dy * dy / (2.0 * shThin * shThin))
                 * exp(-dx * dx / (2.0 * shLen * shLen));
        float h90 = exp(-dx * dx / (2.0 * shThin * shThin))
                  * exp(-dy * dy / (2.0 * shLen * shLen));
        vec3 streakCol = mix(vec3(1.0,0.82,0.45), vec3(0.35,0.55,1.0), 0.3);
        flare += streakCol * (h0 + h90) * streakE * em;
    }

    if (emitterCount == 0) { FragColor = vec4(0.0); return; }

    vec3 avgEmissive = emissiveSum / float(emitterCount);
    float avgEm = max(max(avgEmissive.r, avgEmissive.g), avgEmissive.b);
    {
        vec2 centerDir = uSunPos - vec2(0.5);
        float cDist = max(length(centerDir), 0.001);
        float gSpacing = max(cDist * 2.0, 0.02);
        vec3 gCol[5];
        gCol[0]=vec3(1.0,0.25,0.2); gCol[1]=vec3(1.0,0.55,0.1);
        gCol[2]=vec3(0.3,1.0,0.35); gCol[3]=vec3(0.2,0.6,1.0);
        gCol[4]=vec3(0.65,0.25,1.0);
        for (int i = 0; i < 5; i++) {
            float t  = -(float(i)+1.0) * gSpacing;
            vec2  gp = uSunPos + centerDir * (t / cDist);
            vec2  gv = uv - gp;
            gv.x *= uAspectRatio;
            float gs = max(cDist * (0.28 + float(i)*0.12), 0.006 + float(i)*0.002);
            float gh = smoothstep(gs * 1.35, gs * 0.65, abs(gv.x))
                     * smoothstep(gs * 1.35, gs * 0.65, abs(gv.y));
            flare += gCol[i] * gh * (0.35 - float(i)*0.05) * avgEm;
        }
    }

    flare = flare / float(N_EMITTERS);
    FragColor = vec4(flare * uIntensity, 1.0);
}
