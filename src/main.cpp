#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "config.h"
#include "camera.h"
#include "shader.h"
#include "sphere.h"
#include "planet.h"
#include "texture.h"

Camera* g_camera = nullptr;
float g_lastX, g_lastY;
bool g_firstMouse = true;
float g_deltaTime = 0.0f;
float g_lastFrame = 0.0f;
int g_screenWidth = 1280;
int g_screenHeight = 720;
bool g_bloomEnabled = true;
bool g_directOutput = false;
bool g_testRedSphere = false;
bool g_flareEnabled = false;
bool g_autoExposure = false;        // 默认关闭自动曝光，使用固定曝光
bool g_debugGBuffer = false;       // G 键：直出 G-Buffer 颜色，绕过全部后处理
bool g_extremeDiagnose = false;    // X 键：极端隔离，G-Buffer 后直接 blit 到屏幕
bool g_wireframe = false;          // F 键：线框模式
float g_manualExposure = 0.0f;    // 手动曝光补偿，±2 EV
float g_fixedExposure = 1.0f;      // 固定曝光值（autoExposure=false 时使用）
float g_adaptedLogLum = log(0.05f);  // 自动曝光用，初始化为一个合理值

// ======================= Luminance FBO (auto-exposure) =======================
unsigned int lumFBOs[12], lumTex[12];
unsigned int avgLumTex = 0;
unsigned int lumShader = 0;
int g_numLumPasses = 0;

void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
    g_screenWidth = w;
    g_screenHeight = h;
}
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    float xoffset = xpos - g_lastX;
    float yoffset = g_lastY - ypos;
    g_lastX = xpos; g_lastY = ypos;
    g_camera->processMouseMovement(xoffset, yoffset);
}
void scroll_callback(GLFWwindow*, double, double yoffset) {
    g_camera->processMouseScroll(static_cast<float>(yoffset));
}

void checkGLError(const char* context) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "OpenGL ERROR (" << context << "): 0x" << std::hex << err << std::dec << " | ";
        switch (err) {
            case GL_INVALID_ENUM: std::cerr << "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: std::cerr << "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: std::cerr << "GL_INVALID_OPERATION"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: std::cerr << "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            case GL_OUT_OF_MEMORY: std::cerr << "GL_OUT_OF_MEMORY"; break;
            default: std::cerr << "UNKNOWN"; break;
        }
        std::cerr << std::endl;
    }
}

// ======================== 全屏四边形 ========================
unsigned int quadVAO = 0, quadVBO = 0;
void createQuad() {
    float quadVertices[] = {
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
    };
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    checkGLError("createQuad");
    std::cout << "[DEBUG] Quad VAO: " << quadVAO << std::endl;
}

// ======================== G-Buffer FBO (MRT) ========================
unsigned int gBufferFBO;
unsigned int gColorTex, gNormalTex, gDepthTex;

bool createGBuffer(int w, int h) {
    // --- G-Buffer FBO ---
    glGenFramebuffers(1, &gBufferFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);

    // Attachment 0: HDR color
    glGenTextures(1, &gColorTex);
    glBindTexture(GL_TEXTURE_2D, gColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gColorTex, 0);

    // Attachment 1: view-space normals
    glGenTextures(1, &gNormalTex);
    glBindTexture(GL_TEXTURE_2D, gNormalTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gNormalTex, 0);

    // Depth attachment (texture, not renderbuffer, for SSAO sampling)
    glGenTextures(1, &gDepthTex);
    glBindTexture(GL_TEXTURE_2D, gDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, gDepthTex, 0);

    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ERROR] G-Buffer FBO incomplete! 0x" << std::hex << status << std::dec << std::endl;
        return false;
    }
    std::cout << "[DEBUG] G-Buffer FBO " << gBufferFBO << " complete (" << w << "x" << h << ")" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGLError("createGBuffer");
    return true;
}

// ======================== SSAO FBO ========================
unsigned int ssaoFBO, ssaoTex;
unsigned int noiseTex;

void createSSAOFBO(int w, int h) {
    glGenFramebuffers(1, &ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);

    glGenTextures(1, &ssaoTex);
    glBindTexture(GL_TEXTURE_2D, ssaoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssaoTex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    std::cout << "[DEBUG] SSAO FBO " << ssaoFBO << " " << (status == GL_FRAMEBUFFER_COMPLETE ? "OK" : "FAIL") << std::endl;

    // Noise texture for random rotation (4x4)
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::default_random_engine rng(42);
    std::vector<glm::vec3> noiseData;
    for (int i = 0; i < 16; i++) {
        noiseData.push_back(glm::vec3(dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, 0.0f));
    }
    glGenTextures(1, &noiseTex);
    glBindTexture(GL_TEXTURE_2D, noiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noiseData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGLError("createSSAOFBO");
}

// ======================== Scene FBO (composite output) ========================
unsigned int sceneFBO, sceneTex;

void createSceneFBO(int w, int h) {
    glGenFramebuffers(1, &sceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

    glGenTextures(1, &sceneTex);
    glBindTexture(GL_TEXTURE_2D, sceneTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    std::cout << "[DEBUG] Scene FBO " << sceneFBO << " " << (status == GL_FRAMEBUFFER_COMPLETE ? "OK" : "FAIL") << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGLError("createSceneFBO");
}

// ======================== 模糊乒乓 FBO ========================
unsigned int blurFBO[2], blurTex[2];

bool createBlurFBOs(int w, int h) {
    for (int i = 0; i < 2; i++) {
        glGenFramebuffers(1, &blurFBO[i]);
        glGenTextures(1, &blurTex[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[i]);
        glBindTexture(GL_TEXTURE_2D, blurTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTex[i], 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        std::cout << "[DEBUG] Blur FBO " << i << " (" << blurFBO[i] << ") ";
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "INCOMPLETE! 0x" << std::hex << status << std::dec << std::endl;
            return false;
        }
        std::cout << "OK" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGLError("createBlurFBOs");
    return true;
}

// ======================== 后处理着色器 ========================
const char* postVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 TexCoords;
void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoords = aTexCoords;
}
)";
const char* brightFragSrc = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D sceneTexture;
uniform float threshold;
void main() {
    vec3 color = texture(sceneTexture, TexCoords).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(threshold * 0.5, threshold, brightness);
    FragColor = vec4(color * w, w);
}
)";
const char* blurFragSrc = R"(
#version 330 core
in vec2 TexCoords;
out vec4 FragColor;
uniform sampler2D image;
uniform bool horizontal;
// 17-tap Gaussian, sigma=4, 半径覆盖 ~8px，避免窄核产生环形伪影
uniform float weight[9] = float[] (0.1032, 0.1000, 0.0910, 0.0779, 0.0626, 0.0472, 0.0335, 0.0223, 0.0140);
void main() {
    vec2 tex_offset = 1.0 / textureSize(image, 0);
    vec3 result = texture(image, TexCoords).rgb * weight[0];
    if (horizontal) {
        for (int i = 1; i < 9; i++) {
            result += texture(image, TexCoords + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += texture(image, TexCoords - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
        }
    } else {
        for (int i = 1; i < 9; i++) {
            result += texture(image, TexCoords + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
            result += texture(image, TexCoords - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
        }
    }
    FragColor = vec4(result, 1.0);
}
)";
// final.frag 改为从文件加载（含 auto-exposure）

// ======================== 坐标轴着色器 ========================
const char* axisVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
out vec3 vColor;
out vec3 AxisPos;
uniform mat4 view, projection;
void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    AxisPos = worldPos.xyz;
    vColor = aColor;
}
)";
const char* axisFragSrc = R"(
#version 330 core
in vec3 vColor;
in vec3 AxisPos;
layout(location = 0) out vec4 gColor;
layout(location = 1) out vec4 gNormal;
void main() {
    gColor = vec4(vColor, 1.0);
    gNormal = vec4(0.0, 0.0, 0.0, 0.0);
}
)";

struct AxisVertex { float x,y,z; float r,g,b; };
void createAxisVAO(unsigned int& VAO, unsigned int& VBO, float len) {
    std::vector<AxisVertex> verts = {
        {0,0,0,1,0,0}, {len,0,0,1,0,0},
        {0,0,0,0.4f,0,0}, {-len,0,0,0.4f,0,0},
        {0,0,0,0,1,0}, {0,len,0,0,1,0},
        {0,0,0,0,0.4f,0}, {0,-len,0,0,0.4f,0},
        {0,0,0,0,0,1}, {0,0,len,0,0,1},
        {0,0,0,0,0,0.4f}, {0,0,-len,0,0,0.4f}
    };
    glGenVertexArrays(1, &VAO); glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(AxisVertex), verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(AxisVertex),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(AxisVertex),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    std::cout << "[DEBUG] Axis VAO: " << VAO << std::endl;
    checkGLError("createAxisVAO");
}

const char* redVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 model, view, projection;
void main() { gl_Position = projection * view * model * vec4(aPos, 1.0); }
)";
const char* redFragSrc = R"(
#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }
)";

// ======================== main ========================
int main() {
    Config config;
    if (!config.load("config.ini")) return -1;

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(config.windowWidth, config.windowHeight, "Solar Debug HDR", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;
    glEnable(GL_DEPTH_TEST);
    std::cout << "[DEBUG] OpenGL " << glGetString(GL_VERSION) << " GLSL " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "[DEBUG] Renderer: " << glGetString(GL_RENDERER) << std::endl;

    glfwGetFramebufferSize(window, &g_screenWidth, &g_screenHeight);
    std::cout << "[DEBUG] Window: " << g_screenWidth << "x" << g_screenHeight << std::endl;

    // 着色器
    unsigned int sunShader       = createShaderProgramFromFiles("src/shaders/planet.vert", "src/shaders/sun.frag");
    unsigned int planetShader    = createShaderProgramFromFiles("src/shaders/planet.vert", "src/shaders/planet.frag");
    unsigned int lensFlareShader = createShaderProgramFromFiles("src/shaders/lensflare.vert", "src/shaders/lensflare.frag");
    std::string ssaoSrc      = readShaderFile("src/shaders/ssao.frag");
    std::string compositeSrc = readShaderFile("src/shaders/composite.frag");
    unsigned int ssaoShader      = createShaderProgram(postVertexSrc, ssaoSrc.c_str());
    unsigned int compositeShader = createShaderProgram(postVertexSrc, compositeSrc.c_str());
    unsigned int brightShader    = createShaderProgram(postVertexSrc, brightFragSrc);
    unsigned int blurShader      = createShaderProgram(postVertexSrc, blurFragSrc);
    std::string finalSrc = readShaderFile("src/shaders/final.frag");
    unsigned int finalShader     = createShaderProgram(postVertexSrc, finalSrc.c_str());
    unsigned int axisShader      = createShaderProgram(axisVertSrc, axisFragSrc);
    unsigned int redShader       = createShaderProgram(redVertSrc, redFragSrc);
    unsigned int atmosphereShader = createShaderProgramFromFiles("src/shaders/atmosphere.vert", "src/shaders/atmosphere.frag");
    unsigned int planetGlowShader = createShaderProgramFromFiles("src/shaders/planet_glow.vert", "src/shaders/planet_glow.frag");


    if (!sunShader || !planetShader || !lensFlareShader || !ssaoShader || !compositeShader ||
        !brightShader || !blurShader || !finalShader || !axisShader || !redShader || !atmosphereShader ||
        !planetGlowShader) {
        std::cerr << "Shader compilation failed!" << std::endl;
        return -1;
    }

    createQuad();
    if (!createGBuffer(g_screenWidth, g_screenHeight)) return -1;
    createSSAOFBO(g_screenWidth, g_screenHeight);
    createSceneFBO(g_screenWidth, g_screenHeight);
    if (!createBlurFBOs(g_screenWidth, g_screenHeight)) return -1;

    // ======================= Luminance FBO chain (auto-exposure) =======================
    glGenTextures(1, &avgLumTex);
    glBindTexture(GL_TEXTURE_2D, avgLumTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    {
        int w = g_screenWidth, h = g_screenHeight;
        g_numLumPasses = 0;
        // 持续降采样直到 1x1
        while ((w > 1 || h > 1) && g_numLumPasses < 12) {
            int nw = w > 1 ? w / 2 : 1;
            int nh = h > 1 ? h / 2 : 1;
            glGenTextures(1, &lumTex[g_numLumPasses]);
            glBindTexture(GL_TEXTURE_2D, lumTex[g_numLumPasses]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, nw, nh, 0, GL_RED, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &lumFBOs[g_numLumPasses]);
            glBindFramebuffer(GL_FRAMEBUFFER, lumFBOs[g_numLumPasses]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lumTex[g_numLumPasses], 0);
            { std::string msg = "lumFBOs[" + std::to_string(g_numLumPasses) + "](" + std::to_string(nw) + "x" + std::to_string(nh) + ")"; checkGLError(msg.c_str()); }
            g_numLumPasses++;
            w = nw; h = nh;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    std::cout << "[DEBUG] Luminance FBO chain: " << g_numLumPasses << " passes" << std::endl;

    // Luminance shader
    std::string lumFragSrc = readShaderFile("src/shaders/luminance.frag");
    lumShader = createShaderProgram(postVertexSrc, lumFragSrc.c_str());
    if (!lumShader) { std::cerr << "Luminance shader failed!" << std::endl; return -1; }

    SphereMesh sphere = createSphere(1.0f, 64, 32);
    std::cout << "[DEBUG] Sphere indices: " << sphere.indexCount << std::endl;

    Camera camera(config.cameraPos, config.cameraYaw, config.cameraPitch, config.cameraRoll,
                  config.cameraFov, config.cameraSpeed, config.cameraSensitivity);
    g_camera = &camera;

    std::vector<Planet> planets;
    for (auto& p : config.planets) {
        unsigned int tex = loadTexture(p.texturePath, p.color);
        bool atm = (p.name == "earth");  // 地球有大气层
        planets.emplace_back(p.name, p.orbitRadius, p.orbitPeriod, p.rotationPeriod,
                             p.size, tex, p.color, p.roughness, p.metallic, atm);
    }
    std::cout << "[DEBUG] Planets: " << planets.size() << std::endl;
    glm::vec3 sunPos(0.0f);
    float sunRadius = 15.0f;
    if (!planets.empty()) { sunPos = planets[0].getPosition(); sunRadius = planets[0].getSize(); }

    unsigned int axisVAO, axisVBO;
    createAxisVAO(axisVAO, axisVBO, 40000.0f);

    // SSAO kernel (CPU生成)
    std::uniform_real_distribution<float> rngDist(0.0f, 1.0f);
    std::default_random_engine rng(1337);
    std::vector<glm::vec3> ssaoKernel;
    for (int i = 0; i < 16; i++) {
        glm::vec3 sample(rngDist(rng) * 2.0f - 1.0f, rngDist(rng) * 2.0f - 1.0f, rngDist(rng));
        sample = glm::normalize(sample);
        sample *= rngDist(rng);
        float scale = (float)i / 16.0f;
        scale = 0.1f + scale * scale * 0.9f;
        sample *= scale;
        ssaoKernel.push_back(sample);
    }

    bool bPressedLast = false, nPressedLast = false, tPressedLast = false;
    bool lPressedLast = false, plusPressedLast = false, minusPressedLast = false;
    bool zPressedLast = false, gPressedLast = false, fPressedLast = false, xPressedLast = false;


    float lightIntensity = 5000.0f;   // 线性衰减 1/dist，地球 ≈ 5.0
    float flareIntensity = 1.2f;
    float ssaoStrength = 0.8f;
    float coronaIntensity = 0.5f;
    float kSunRadius = 15.0f;  // 与 config.ini 同步，用于角度计算

    while (!glfwWindowShouldClose(window)) {
        float currentTime = glfwGetTime();
        g_deltaTime = currentTime - g_lastFrame;
        g_lastFrame = currentTime;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS && !bPressedLast) {
            g_bloomEnabled = !g_bloomEnabled;
            std::cout << "[INFO] Bloom: " << (g_bloomEnabled ? "ON" : "OFF") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS && !nPressedLast) {
            g_directOutput = !g_directOutput;
            std::cout << "[INFO] Direct output: " << (g_directOutput ? "ON" : "OFF") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !tPressedLast) {
            g_testRedSphere = !g_testRedSphere;
            std::cout << "[INFO] Test red sphere: " << (g_testRedSphere ? "ON" : "OFF") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS && !lPressedLast) {
            g_flareEnabled = !g_flareEnabled;
            std::cout << "[INFO] Lens flare: " << (g_flareEnabled ? "ON" : "OFF") << std::endl;
        }
        // 手动曝光补偿
        if (glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS && !plusPressedLast) {
            g_manualExposure += 0.5f;
            std::cout << "[INFO] Manual exposure: " << g_manualExposure << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS && !minusPressedLast) {
            g_manualExposure -= 0.5f;
            std::cout << "[INFO] Manual exposure: " << g_manualExposure << std::endl;
        }
        // Z 键切换自动/固定曝光
        if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS && !zPressedLast) {
            g_autoExposure = !g_autoExposure;
            std::cout << "[INFO] Auto exposure: " << (g_autoExposure ? "ON" : "OFF (fixed)") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !gPressedLast) {
            g_debugGBuffer = !g_debugGBuffer;
            std::cout << "[INFO] G-Buffer direct: " << (g_debugGBuffer ? "ON" : "OFF") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !fPressedLast) {
            g_wireframe = !g_wireframe;
            std::cout << "[INFO] Wireframe: " << (g_wireframe ? "ON" : "OFF") << std::endl;
        }
        if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS && !xPressedLast) {
            g_extremeDiagnose = !g_extremeDiagnose;
            std::cout << "[INFO] Extreme diagnose: " << (g_extremeDiagnose ? "ON" : "OFF") << std::endl;
        }
        bPressedLast = (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS);
        nPressedLast = (glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS);
        tPressedLast = (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS);
        lPressedLast = (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS);
        zPressedLast = (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS);
        gPressedLast = (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS);
        fPressedLast = (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS);
        xPressedLast = (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS);
        plusPressedLast = (glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS);
        minusPressedLast = (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS);

        camera.processKeyboard(GLFW_KEY_W, glfwGetKey(window, GLFW_KEY_W), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_A, glfwGetKey(window, GLFW_KEY_A), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_S, glfwGetKey(window, GLFW_KEY_S), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_D, glfwGetKey(window, GLFW_KEY_D), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_Q, glfwGetKey(window, GLFW_KEY_Q), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_E, glfwGetKey(window, GLFW_KEY_E), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_R, glfwGetKey(window, GLFW_KEY_R), g_deltaTime);
        camera.processKeyboard(GLFW_KEY_F, glfwGetKey(window, GLFW_KEY_F), g_deltaTime);
        camera.update(g_deltaTime);

        float simTime = (float)glfwGetTime() * config.timeScale;
        for (auto& p : planets) p.update(simTime);

        glm::vec3 viewPos = camera.getPosition();
        glm::mat4 view = camera.getViewMatrix();

        // dynamic near plane: adapt to camera proximity to nearest planet surface
        float dynamicNear = 0.1f;
        for (auto& p : planets) {
            float distToSurface = glm::length(p.getPosition() - viewPos) - p.getSize();
            if (distToSurface < 0.0f) distToSurface = 0.001f;
            float nearForThis = distToSurface * 0.5f;
            if (nearForThis < dynamicNear) dynamicNear = nearForThis;
        }
        if (dynamicNear < 0.001f) dynamicNear = 0.001f;
        glm::mat4 proj = glm::perspective(glm::radians(camera.getFov()),
                                          (float)g_screenWidth / g_screenHeight,
                                          dynamicNear, 80000.0f);

        // ================================================================
        // Pass 1: G-Buffer 渲染 (MRT: color + normal + depth)
        // ================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
        GLenum gBufDraw[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, gBufDraw);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        checkGLError("G-Buffer clear");

        // 太阳：纯 emissive，写 G-Buffer
        for (auto& p : planets) {
            if (p.isSun()) {
                p.drawEmissive(sunShader, sphere.VAO, sphere.indexCount, view, proj, viewPos, 10.0f);
            }
        }
        // 诊断：线框模式
        if (g_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        // 行星：从太阳接收光照，每个面独立计算 PBR
        for (auto& p : planets) {
            if (!p.isSun()) {
                p.draw(planetShader, sphere.VAO, sphere.indexCount, view, proj, viewPos, lightIntensity, sunRadius);
            }
        }
        checkGLError("planet draw");
        if (g_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // ===== 大气散射 (Rayleigh + Mie) — additive 叠加 =====
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glUseProgram(atmosphereShader);
        for (auto& p : planets) {
            if (p.hasAtmosphere()) {
                float pSize = p.getSize();
                float atmosR = pSize * 1.15f;
                glm::mat4 atmosM = glm::translate(glm::mat4(1.0f), p.getPosition());
                atmosM = glm::scale(atmosM, glm::vec3(atmosR));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "model"), 1, GL_FALSE, glm::value_ptr(atmosM));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "planetCenter"), 1, glm::value_ptr(p.getPosition()));
                glUniform1f(glGetUniformLocation(atmosphereShader, "planetRadius"), pSize);
                glUniform1f(glGetUniformLocation(atmosphereShader, "atmosphereRadius"), atmosR);
                glm::vec3 sunDN = glm::normalize(sunPos - p.getPosition());
                glUniform3fv(glGetUniformLocation(atmosphereShader, "sunDir"), 1, glm::value_ptr(sunDN));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "cameraPos"), 1, glm::value_ptr(viewPos));
                glBindVertexArray(sphere.VAO);
                glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        checkGLError("atmosphere pass");

        // ===== 坐标轴 (写入 G-Buffer, 深度测试开启, 被天体自然遮挡) =====
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDrawBuffers(2, gBufDraw);
        glUseProgram(axisShader);
        glUniformMatrix4fv(glGetUniformLocation(axisShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(axisShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        glBindVertexArray(axisVAO);
        glDrawArrays(GL_LINES, 0, 12);
        glBindVertexArray(0);

        if (g_testRedSphere) {
            glUseProgram(redShader);
            glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(10.0f));
            glUniformMatrix4fv(glGetUniformLocation(redShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(redShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(redShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
            glBindVertexArray(sphere.VAO);
            glDrawElements(GL_TRIANGLES, sphere.indexCount, GL_UNSIGNED_INT, 0);
            checkGLError("red sphere draw");
        }

        // ===== G-Buffer 直出诊断：绕过全部后处理 =====
        if (g_debugGBuffer) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, g_screenWidth, g_screenHeight,
                              0, 0, g_screenWidth, g_screenHeight,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue;
        }

        // ===== 极端隔离：G-Buffer 后直接 blit，跳过所有后续 pass =====
        if (g_extremeDiagnose) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(0, 0, g_screenWidth, g_screenHeight,
                              0, 0, g_screenWidth, g_screenHeight,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue;
        }

        // ================================================================
        // Pass 2: SSAO
        // ================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, ssaoFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(ssaoShader);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gNormalTex);
        glUniform1i(glGetUniformLocation(ssaoShader, "gNormal"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gDepthTex);
        glUniform1i(glGetUniformLocation(ssaoShader, "gDepth"), 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, noiseTex);
        glUniform1i(glGetUniformLocation(ssaoShader, "noiseTex"), 2);

        glm::mat4 invProj = glm::inverse(proj);
        glUniformMatrix4fv(glGetUniformLocation(ssaoShader, "invProj"), 1, GL_FALSE, glm::value_ptr(invProj));
        glUniformMatrix4fv(glGetUniformLocation(ssaoShader, "proj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniform2f(glGetUniformLocation(ssaoShader, "screenSize"), (float)g_screenWidth, (float)g_screenHeight);

        for (int i = 0; i < 16; i++) {
            std::string name = "samples[" + std::to_string(i) + "]";
            glUniform3fv(glGetUniformLocation(ssaoShader, name.c_str()), 1, glm::value_ptr(ssaoKernel[i]));
        }
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        checkGLError("SSAO pass");

        // ================================================================
        // Pass 3: 合成 (scene * SSAO + corona + 日食) → sceneFBO
        // ================================================================
        // 计算太阳屏幕坐标（供 composite shader 和后续 lens flare 使用）
        float sunScreenX = -1.0f, sunScreenY = -1.0f;
        float sunUVRadius = 0.05f;
        bool sunDiskOnScreen = false;
        glm::vec4 sunClip = proj * view * glm::vec4(sunPos, 1.0f);
        if (sunClip.w > 0.0f) {
            glm::vec3 ndc = glm::vec3(sunClip) / sunClip.w;
            float distToSun = glm::length(sunPos - viewPos);
            float sunAngRadius = glm::atan(kSunRadius / distToSun);
            float fovY = glm::radians(camera.getFov());
            float sunNDCRadius = glm::tan(sunAngRadius) / glm::tan(fovY * 0.5f);
            sunDiskOnScreen = (ndc.x + sunNDCRadius > -1.0f && ndc.x - sunNDCRadius < 1.0f &&
                                ndc.y + sunNDCRadius > -1.0f && ndc.y - sunNDCRadius < 1.0f);
            sunScreenX = (ndc.x + 1.0f) * 0.5f;
            sunScreenY = (ndc.y + 1.0f) * 0.5f;
            sunUVRadius = sunNDCRadius * 0.5f;
            if (sunUVRadius < 0.0005f) sunUVRadius = 0.0005f;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(compositeShader);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gColorTex);
        glUniform1i(glGetUniformLocation(compositeShader, "gColor"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ssaoTex);
        glUniform1i(glGetUniformLocation(compositeShader, "ssaoTex"), 1);

        float actualCoronaIntensity = coronaIntensity;
        if (sunClip.w > 0.0f) {
            // 太阳靠近屏幕边缘时平滑淡出，避免硬切
            glm::vec3 ndc = glm::vec3(sunClip) / sunClip.w;
            float edgeDist = 1.0f;
            edgeDist = std::min(edgeDist, glm::smoothstep(0.0f, 0.3f, ndc.x + 1.0f));
            edgeDist = std::min(edgeDist, glm::smoothstep(0.0f, 0.3f, 1.0f - ndc.x));
            edgeDist = std::min(edgeDist, glm::smoothstep(0.0f, 0.3f, ndc.y + 1.0f));
            edgeDist = std::min(edgeDist, glm::smoothstep(0.0f, 0.3f, 1.0f - ndc.y));
            actualCoronaIntensity *= edgeDist;
        } else {
            actualCoronaIntensity = 0.0f;
        }
        glUniform1f(glGetUniformLocation(compositeShader, "uSSAOStrength"), ssaoStrength);
        glUniform2f(glGetUniformLocation(compositeShader, "uSunPos"), sunScreenX, sunScreenY);
        glUniform1f(glGetUniformLocation(compositeShader, "uSunRadius"), sunUVRadius);
        glUniform1f(glGetUniformLocation(compositeShader, "uIntensity"), actualCoronaIntensity);
        glUniform1f(glGetUniformLocation(compositeShader, "uAspectRatio"), (float)g_screenWidth / g_screenHeight);
        glUniform3f(glGetUniformLocation(compositeShader, "uSunColor"), 1.0f, 0.85f, 0.2f);

        // 传入凌日行星数据
        {
            float fovY = glm::radians(camera.getFov());
            int eclipseCount = 0;
            GLint countLoc = glGetUniformLocation(compositeShader, "uEclipseCount");
            GLint centerLoc = glGetUniformLocation(compositeShader, "uEclipseCenter[0]");
            GLint radiusLoc = glGetUniformLocation(compositeShader, "uEclipseRadius[0]");

            for (auto& p : planets) {
                if (p.isSun()) continue;
                if (eclipseCount >= 8) break;
                glm::vec3 planetPos = p.getPosition();
                glm::vec3 toSun    = sunPos - planetPos;
                glm::vec3 toCamera = viewPos - planetPos;
                if (glm::dot(toSun, toCamera) >= 0.0f) continue;
                glm::vec4 clip = proj * view * glm::vec4(planetPos, 1.0f);
                if (clip.w <= 0.0f) continue;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                float distToPlanet = glm::length(planetPos - viewPos);
                // 有大气层的行星：日冕过滤范围 = 大气球体半径 (pSize*1.15)，与渲染边界对齐
                // 无大气层的行星：日冕过滤范围 = 行星半径 +8% 边距
                float bodySize = p.hasAtmosphere() ? p.getSize() * 1.15f : p.getSize();
                float angRadius = glm::atan(bodySize / distToPlanet);
                float ndcRadius = glm::tan(angRadius) / glm::tan(fovY * 0.5f);
                // 大气球体已有精确的渲染半径，无需膨胀；普通行星留 8% 防边缘泄漏
                float inflate = p.hasAtmosphere() ? 1.0f : 1.08f;
                ndcRadius *= inflate;
                glUniform2f(centerLoc + eclipseCount, ndc.x, ndc.y);
                glUniform1f(radiusLoc + eclipseCount, ndcRadius);
                eclipseCount++;
            }
            glUniform1i(countLoc, eclipseCount);
        }

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        checkGLError("composite pass");

        // ================================================================
        // Pass 3.5: 计算平均亮度 (auto-exposure)
        // ================================================================
        glUseProgram(lumShader);
        int lumSrcTex = sceneTex;
        int lumSrcW = g_screenWidth, lumSrcH = g_screenHeight;
        bool lumFirstPass = true;
        for (int i = 0; i < g_numLumPasses; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, lumFBOs[i]);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1i(glGetUniformLocation(lumShader, "firstPass"), lumFirstPass ? 1 : 0);
            glUniform2f(glGetUniformLocation(lumShader, "inputRes"), (float)lumSrcW, (float)lumSrcH);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, lumSrcTex);
            glUniform1i(glGetUniformLocation(lumShader, "inputTex"), 0);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            lumSrcTex = lumTex[i];
            lumSrcW /= 2; lumSrcH /= 2;
            lumFirstPass = false;
        }
        // 自动曝光模式：读回亮度并做时间平滑
        if (g_autoExposure) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, lumFBOs[g_numLumPasses - 1]);
            float rawLogLum;
            glReadPixels(0, 0, 1, 1, GL_RED, GL_FLOAT, &rawLogLum);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

            float delta = rawLogLum - g_adaptedLogLum;
            float adaptSpeed = (delta > 0.0f) ? 0.25f : 0.5f;
            float t = glm::min(adaptSpeed * g_deltaTime, 1.0f);
            delta *= t;
            float maxDelta = 0.08f;
            delta = glm::clamp(delta, -maxDelta, maxDelta);
            g_adaptedLogLum += delta;

            glBindTexture(GL_TEXTURE_2D, avgLumTex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RED, GL_FLOAT, &g_adaptedLogLum);
        }
        // 固定曝光模式：不做任何适应，avgLumTex 保持初始值
        checkGLError("luminance pass");

        // ================================================================
        // Pass 4: 亮度提取 → blurFBO[0]
        // ================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(brightShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glUniform1i(glGetUniformLocation(brightShader, "sceneTexture"), 0);
        glUniform1f(glGetUniformLocation(brightShader, "threshold"), 8.0f);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        checkGLError("bright pass");

        // ================================================================
        // Pass 5: 高斯模糊 (bright提取完在 blurFBO[0], 来回 blur)
        // ================================================================
        bool horizontal = true;
        unsigned int blurAmount = 4;
        for (unsigned int i = 0; i < blurAmount * 2; i++) {
            int srcIdx = (i == 0) ? 0 : ((i % 2 == 1) ? 1 : 0);
            int dstIdx = (i == 0) ? 1 : 1 - (i % 2);
            glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[dstIdx]);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(blurShader);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, blurTex[srcIdx]);
            glUniform1i(glGetUniformLocation(blurShader, "image"), 0);
            glUniform1i(glGetUniformLocation(blurShader, "horizontal"), horizontal);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            horizontal = !horizontal;
        }
        checkGLError("blur pass");

        // ================================================================
        // Pass 6: 最终合成到屏幕 (auto-exposure)
        // ================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(finalShader);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glUniform1i(glGetUniformLocation(finalShader, "sceneTexture"), 0);
        glActiveTexture(GL_TEXTURE1);
        int finalBlurIdx = 0;  // 偶数次迭代后结果总在 blurTex[0]
        glBindTexture(GL_TEXTURE_2D, blurTex[finalBlurIdx]);
        glUniform1i(glGetUniformLocation(finalShader, "bloomTexture"), 1);
        // Exposure: 传递 auto/manual 模式参数
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, avgLumTex);
        glUniform1i(glGetUniformLocation(finalShader, "avgLumTex"), 2);
        glUniform1i(glGetUniformLocation(finalShader, "autoExposure"), g_autoExposure ? 1 : 0);
        glUniform1f(glGetUniformLocation(finalShader, "fixedExposure"), g_fixedExposure);
        glUniform1f(glGetUniformLocation(finalShader, "manualExposure"), g_manualExposure);
        glUniform1f(glGetUniformLocation(finalShader, "bloomStrength"), g_bloomEnabled ? 0.4f : 0.0f);
        glUniform1i(glGetUniformLocation(finalShader, "directOutput"), g_directOutput ? 1 : 0);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_DEPTH_TEST);
        checkGLError("final pass");

        // ================================================================
        // Pass 6.5: 远处行星辉光 (叠加混合)
        // 当行星屏幕投影半径 < 8px 时，叠加高斯光晕使其可见
        // ================================================================
        {
            GLint pgCenterLoc  = glGetUniformLocation(planetGlowShader, "uCenter");
            GLint pgSizeLoc    = glGetUniformLocation(planetGlowShader, "uSize");
            GLint pgColorLoc   = glGetUniformLocation(planetGlowShader, "uColor");
            GLint pgIntensityLoc = glGetUniformLocation(planetGlowShader, "uIntensity");

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(planetGlowShader);

            // 绑定 G-Buffer 深度用于 CPU 遮挡检测
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);

            float fovY = glm::radians(camera.getFov());
            float screenHeight = (float)g_screenHeight;
            float screenWidth  = (float)g_screenWidth;

            for (auto& p : planets) {
                if (p.isSun()) continue;

                glm::vec3 planetPos = p.getPosition();
                glm::vec4 clip = proj * view * glm::vec4(planetPos, 1.0f);
                if (clip.w <= 0.0f) continue;  // 相机背后

                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (abs(ndc.x) > 1.2f || abs(ndc.y) > 1.2f) continue;  // 屏外（留 20% 余量）

                // 深度遮挡检测：采样 G-Buffer 深度，如果前方有物体则跳过辉光
                {
                    int px = (int)((ndc.x + 1.0f) * 0.5f * screenWidth);
                    int py = (int)((ndc.y + 1.0f) * 0.5f * screenHeight);
                    float sceneDepth;
                    glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &sceneDepth);
                    // NDC z [-1,1] → depth [0,1]; sceneDepth < planetDepth 表示前方有遮挡
                    float planetDepth = ndc.z * 0.5f + 0.5f;
                    if (sceneDepth < planetDepth - 0.0001f) continue;  // 被遮挡
                }

                // 屏幕投影像素半径
                float distToPlanet = glm::length(planetPos - viewPos);
                float angRadius = glm::atan(p.getSize() / distToPlanet);
                float ndcRadius = glm::tan(angRadius) / glm::tan(fovY * 0.5f);
                float pixelRadius = ndcRadius * 0.5f * screenHeight;

                // 暗面检测：相机在行星暗面时不渲染辉光
                glm::vec3 toSun    = sunPos - planetPos;
                glm::vec3 toCamera = viewPos - planetPos;
                if (glm::dot(toSun, toCamera) < 0.0f) continue;

                // < 20px 的行星加光晕（覆盖中远距离）
                if (pixelRadius > 20.0f) continue;

                // 光晕大小：行星越小，光晕越大
                float glowSize = ndcRadius * 6.0f + 0.01f;
                glowSize = glm::clamp(glowSize, 0.01f, 0.10f);

                // 强度：基于行星反照率 + 距离因子
                float albedo = glm::dot(p.getColor(), glm::vec3(0.3f, 0.59f, 0.11f));
                float glowIntensity = albedo * (1.0f - pixelRadius / 20.0f) * 0.8f;
                glowIntensity = glm::clamp(glowIntensity, 0.05f, 0.6f);

                glUniform2f(pgCenterLoc, ndc.x, ndc.y);
                glUniform1f(pgSizeLoc, glowSize);
                glUniform3f(pgColorLoc, p.getColor().r, p.getColor().g, p.getColor().b);
                glUniform1f(pgIntensityLoc, glowIntensity);

                glBindVertexArray(quadVAO);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            checkGLError("planet glow pass");
        }

        // ================================================================
        // Pass 7: 镜头炫光 (叠加混合)
        // ================================================================
        if (g_flareEnabled) {
                float sunVisible = 0.0f;
            if (sunDiskOnScreen) {
                float toLeft   = sunScreenX;
                float toRight  = 1.0f - sunScreenX;
                float toBottom = sunScreenY;
                float toTop    = 1.0f - sunScreenY;
                float nearestEdge = fminf(fminf(toLeft, toRight), fminf(toBottom, toTop));
                sunVisible = glm::smoothstep(-sunUVRadius, sunUVRadius, nearestEdge);

                // CPU 端角度遮挡检测：检查是否有行星在太阳前面（角距离）
                if (sunVisible > 0.01f) {
                    glm::vec3 cameraPos = camera.getPosition();
                    glm::vec3 toSun = sunPos - cameraPos;
                    float distToSun = glm::length(toSun);
                    if (distToSun > 0.1f) {
                        glm::vec3 sunDir = toSun / distToSun;
                        float sunAngRadius = glm::atan(kSunRadius / distToSun);  // 太阳角半径
                        float worstOcclude = 0.0f;
                        for (size_t i = 1; i < planets.size(); i++) {  // 跳过太阳自身
                            glm::vec3 planetPos = planets[i].getPosition();
                            glm::vec3 toPlanet = planetPos - cameraPos;
                            float distToPlanet = glm::length(toPlanet);
                            if (distToPlanet < distToSun) {  // 行星在太阳前面
                                glm::vec3 planetDir = toPlanet / distToPlanet;
                                float cosAng = glm::clamp(glm::dot(planetDir, sunDir), -1.0f, 1.0f);
                                float angSep = glm::acos(cosAng);  // 角距离
                                float planetAngRadius = glm::atan(planets[i].getSize() / distToPlanet) * 1.08f;
                                if (angSep < planetAngRadius + sunAngRadius) {
                                    float overlap = (planetAngRadius + sunAngRadius) - angSep;
                                    float occludeRatio = overlap / (2.0f * sunAngRadius);
                                    float occludeFactor = glm::smoothstep(0.3f, 1.0f, occludeRatio);
                                    worstOcclude = fmaxf(worstOcclude, occludeFactor);
                                }
                            }
                        }
                        // planet完全遮挡 → 炫光完全消失
                        sunVisible = sunVisible * (1.0f - worstOcclude);
                    }
                }
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(lensFlareShader);
            checkGLError("lens flare useProgram");
            glUniform2f(glGetUniformLocation(lensFlareShader, "uSunPos"), sunScreenX, sunScreenY);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uSunVisible"), sunVisible);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uSunRadius"), sunUVRadius);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uIntensity"), flareIntensity);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uAspectRatio"), (float)g_screenWidth / g_screenHeight);
            checkGLError("lens flare uniforms");
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            checkGLError("lens flare drawArrays");
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &axisVAO);
    glDeleteBuffers(1, &axisVBO);
    deleteSphere(sphere);
    glDeleteProgram(sunShader);
    glDeleteProgram(planetShader);
    glDeleteProgram(lensFlareShader);
    glDeleteProgram(ssaoShader);
    glDeleteProgram(compositeShader);
    glDeleteProgram(brightShader);
    glDeleteProgram(blurShader);
    glDeleteProgram(finalShader);
    glDeleteProgram(axisShader);
    glDeleteProgram(redShader);
    glDeleteProgram(atmosphereShader);
    glDeleteProgram(planetGlowShader);
    glfwTerminate();
    return 0;
}
