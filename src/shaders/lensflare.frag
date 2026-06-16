#version 330 core
in vec2 TexCoords;
out vec4 FragColor;

uniform vec2  uSunPos;
uniform float uSunVisible;
uniform float uSunRadius;
uniform float uIntensity;
uniform float uAspectRatio;

void main() {
    vec2 uv = TexCoords;
    if (uSunVisible <= 0.001) { FragColor = vec4(0.0); return; }

    float r = uSunRadius;

    vec2 toSun = uv - uSunPos;
    toSun.x *= uAspectRatio;
    float dist = length(toSun);

    vec3 flare = vec3(0.0);
    float baseR = max(r, 0.0001);
    float nd = dist / baseR;

    // core glow
    float glow = 1.0 / (1.0 + nd * nd * 1.5);
    vec3 glowColor = mix(vec3(1.0, 0.28, 0.04), vec3(1.0, 0.92, 0.60), 1.0 / (1.0 + nd * 0.7));
    flare += glowColor * glow * 1.1;

    // anamorphic streak
    float shVsigma = max(r * 0.38, 0.0001);
    float shV = exp(-toSun.y * toSun.y / (2.0 * shVsigma * shVsigma));
    float shLen = max(0.4 - r * 1.5, 0.005);
    float shH = exp(-toSun.x * toSun.x * uAspectRatio * uAspectRatio / (2.0 * shLen * shLen));
    float hT = abs(toSun.x * uAspectRatio) / max(shLen, 0.001);
    vec3 shColor = mix(vec3(1.0,0.82,0.45), vec3(0.35,0.55,1.0), clamp(hT,0.0,1.0));
    flare += shColor * shV * shH * 2.0;

    // ghosts
    vec2 centerDir = uSunPos - vec2(0.5);
    float cDist = max(length(centerDir), 0.001);
    float gSpacing = max(r * 3.2, 0.04);
    vec3 gCol[5];
    gCol[0]=vec3(1.0,0.25,0.2); gCol[1]=vec3(1.0,0.55,0.1); gCol[2]=vec3(0.3,1.0,0.35);
    gCol[3]=vec3(0.2,0.6,1.0); gCol[4]=vec3(0.65,0.25,1.0);
    for (int i=0; i<5; i++) {
        float t=-(float(i)+1.0)*gSpacing;
        vec2 gp=uSunPos+centerDir*(t/cDist);
        vec2 gv=uv-gp; gv.x*=uAspectRatio;
        float gd=length(gv);
        float gs=max(r*(0.28+float(i)*0.12), 0.006+float(i)*0.002);
        float gh=exp(-gd*gd/(2.0*gs*gs));
        flare+=gCol[i]*gh*(0.35-float(i)*0.05);
    }

    FragColor = vec4(flare * uIntensity * uSunVisible, 1.0);
}