#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <random>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "config.h"
#include "camera.h"
#include "shader.h"
#include "sphere.h"
#include "planet.h"
#include "texture.h"
#include "text_renderer.h"
#include "menu.h"

Camera* g_camera = nullptr;
float* g_pTimeScale = nullptr;  // 指向 config.timeScale，供 Shift+滚轮 调整
float g_lastX, g_lastY;
bool g_firstMouse = true;
float g_deltaTime = 0.0f;
float g_lastFrame = 0.0f;
double g_simTime = 0.0;  // 累积模拟时间（秒），受 timeScale 控制流速。double 防止精度丢失导致行星瞬移
int g_screenWidth = 1280;
int g_screenHeight = 720;
bool g_bloomEnabled = true;
bool g_flareEnabled = false;
bool g_autoExposure = false;        // 默认关闭自动曝光，使用固定曝光
bool g_wireframe = false;          // F 键：线框模式
float g_manualExposure = 0.0f;    // 手动曝光补偿，±2 EV
float g_fixedExposure = 1.0f;      // 固定曝光值（autoExposure=false 时使用）
int   g_resolutionIndex = 0;         // 当前分辨率索引
bool  g_fullscreen = false;           // 是否全屏
float g_adaptedLogLum = log(0.05f);  // 自动曝光用，初始化为一个合理值
float g_smoothedEmissive = 0.0f;     // lensflare 用，太阳 emissive 亮度的 EMA 平滑值
TextRenderer* g_textRenderer = nullptr;
int g_lastFps = 0;
Menu g_menu;

// ======================= Luminance FBO (auto-exposure) =======================
unsigned int lumFBOs[12], lumTex[12];
unsigned int avgLumTex = 0;
unsigned int lumShader = 0;
int g_numLumPasses = 0;
bool g_fboDirty = false;  // FBO 需要重建

void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    if (w <= 0 || h <= 0) return;  // 忽略最小化窗口（如QQ截图）的 0x0 回调
    glViewport(0, 0, w, h);
    g_screenWidth = w;
    g_screenHeight = h;
    if (g_textRenderer) g_textRenderer->resize(w, h);
    g_fboDirty = true;
}
void mouse_callback(GLFWwindow*, double xpos, double ypos) {
    if (!g_menu.isGameplay()) {
        g_menu.handleMousePos(xpos, ypos);
        return;
    }
    if (g_firstMouse) { g_lastX = xpos; g_lastY = ypos; g_firstMouse = false; }
    float xoffset = xpos - g_lastX;
    float yoffset = g_lastY - ypos;
    g_lastX = xpos; g_lastY = ypos;
    g_camera->processMouseMovement(xoffset, yoffset);
}
void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    if (!g_menu.isGameplay()) {
        g_menu.handleMouseButton(button, action);
    }
}
void scroll_callback(GLFWwindow* window, double, double yoffset) {
    if (!g_menu.isGameplay()) {
        g_menu.handleScroll(yoffset);
        return;
    }
    // Shift + 滚轮：等比调整时间流速
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        if (g_pTimeScale && *g_pTimeScale > 0.0f) {
            float factor = 1.0f + (float)yoffset * 0.5f;
            *g_pTimeScale *= factor;
            if (*g_pTimeScale < 0.001f) *g_pTimeScale = 0.001f;
        }
        return;
    }
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
unsigned int pointVAO = 0, pointVBO = 0;  // sub-pixel 天体点精灵
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

// ======================== Sun Emissive FBO (独立太阳亮度) ========================
unsigned int sunEmissiveFBO, sunEmissiveTex, sunEmissiveDepth;

void createSunEmissiveFBO(int w, int h) {
    glGenFramebuffers(1, &sunEmissiveFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, sunEmissiveFBO);

    glGenTextures(1, &sunEmissiveTex);
    glBindTexture(GL_TEXTURE_2D, sunEmissiveTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sunEmissiveTex, 0);

    // 独立深度 buffer
    glGenRenderbuffers(1, &sunEmissiveDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, sunEmissiveDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sunEmissiveDepth);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    checkGLError("createSunEmissiveFBO");
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

// ======================== 重建所有 FBO（分辨率变更时调用）=======================
void deleteFBOs() {
    glDeleteFramebuffers(1, &gBufferFBO);  gBufferFBO = 0;
    glDeleteTextures(1, &gColorTex);    gColorTex = 0;
    glDeleteTextures(1, &gNormalTex);  gNormalTex = 0;
    glDeleteTextures(1, &gDepthTex);    gDepthTex = 0;
    glDeleteFramebuffers(1, &ssaoFBO);    ssaoFBO = 0;
    glDeleteTextures(1, &ssaoTex);      ssaoTex = 0;
    glDeleteFramebuffers(1, &sunEmissiveFBO); sunEmissiveFBO = 0;
    glDeleteTextures(1, &sunEmissiveTex);    sunEmissiveTex = 0;
    glDeleteRenderbuffers(1, &sunEmissiveDepth); sunEmissiveDepth = 0;
    glDeleteFramebuffers(1, &sceneFBO);   sceneFBO = 0;
    glDeleteTextures(1, &sceneTex);     sceneTex = 0;
    for (int i = 0; i < 2; i++) {
        glDeleteFramebuffers(1, &blurFBO[i]);  blurFBO[i] = 0;
        glDeleteTextures(1, &blurTex[i]);    blurTex[i] = 0;
    }
    for (int i = 0; i < g_numLumPasses; i++) {
        glDeleteFramebuffers(1, &lumFBOs[i]); lumFBOs[i] = 0;
        glDeleteTextures(1, &lumTex[i]);    lumTex[i] = 0;
    }
    g_numLumPasses = 0;
    glDeleteTextures(1, &avgLumTex);  avgLumTex = 0;
}

bool recreateFBOs(int w, int h) {
    deleteFBOs();
    if (!createGBuffer(w, h))  return false;
    createSSAOFBO(w, h);
    createSceneFBO(w, h);
    createSunEmissiveFBO(w, h);
    if (!createBlurFBOs(w, h)) return false;
    // 重建 Luminance 链
    glGenTextures(1, &avgLumTex);
    glBindTexture(GL_TEXTURE_2D, avgLumTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    {
        int tw = w, th = h;
        g_numLumPasses = 0;
        while ((tw > 1 || th > 1) && g_numLumPasses < 12) {
            int nw = tw > 1 ? tw / 2 : 1;
            int nh = th > 1 ? th / 2 : 1;
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
            checkGLError("lumFBO recreate");
            g_numLumPasses++;
            tw = nw; th = nh;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    g_adaptedLogLum = log(0.05f);  // 重置自适应曝光
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
    g_pTimeScale = &config.timeScale;
    if (!config.load("config.ini")) return -1;

    // 从配置文件初始化全局设置
    g_bloomEnabled   = config.bloomEnabled;
    g_flareEnabled   = config.flareEnabled;
    g_autoExposure   = config.autoExposure;
    g_wireframe      = config.wireframe;
    g_manualExposure = config.manualExposure;
    g_resolutionIndex = config.resolutionIndex;
    g_fullscreen     = config.fullscreen;

    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(config.windowWidth, config.windowHeight, "Solar System", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return -1;
    glEnable(GL_DEPTH_TEST);
    // ======================= 加载进度条 =======================
    const char* progVertSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vNDC;
void main() {
    vNDC = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";
    const char* progFragSrc = R"(#version 330 core
in vec2 vNDC;
out vec4 FragColor;
uniform float uProgress;
uniform vec3 uBgColor;
uniform vec3 uBarColor;
uniform vec3 uBorderColor;
uniform float uBarY;
uniform float uBarH;
uniform float uBarW;
void main() {
    // NDC [-1,1]: center=0, uBarW = half-width (0.94 → bar spans -0.94..0.94 = 94% of screen)
    float barLeft   = -uBarW;
    float barRight  = uBarW;
    float barTop    = uBarY + uBarH * 0.5;
    float barBottom = uBarY - uBarH * 0.5;
    float border = 0.003;
    bool inBorder = (vNDC.x >= barLeft - border && vNDC.x <= barRight + border &&
                     vNDC.y >= barBottom - border && vNDC.y <= barTop + border);
    bool inBar = (vNDC.x >= barLeft && vNDC.x <= barRight &&
                  vNDC.y >= barBottom && vNDC.y <= barTop);
    float fillRight = barLeft + 2.0 * uBarW * uProgress;
    if (vNDC.x >= barLeft && vNDC.x <= fillRight && inBar) {
        FragColor = vec4(uBarColor, 1.0);
    } else if (inBar && vNDC.x > fillRight) {
        FragColor = vec4(uBgColor * 0.3, 0.7);
    } else if (inBorder && !inBar) {
        FragColor = vec4(uBorderColor, 0.6);
    } else {
        discard;
    }
}
)";
    unsigned int progShader = createShaderProgram(progVertSrc, progFragSrc);
    unsigned int progVAO, progVBO;
    { float q[] = {-1,-1, 1,-1, -1,1, 1,1};
      glGenVertexArrays(1, &progVAO); glGenBuffers(1, &progVBO);
      glBindVertexArray(progVAO); glBindBuffer(GL_ARRAY_BUFFER, progVBO);
      glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
      glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0); glEnableVertexAttribArray(0); }
    GLint g_progColorLoc = glGetUniformLocation(progShader, "uBarColor");
    GLint g_progProgressLoc = glGetUniformLocation(progShader, "uProgress");
    GLint g_progBarYLoc = glGetUniformLocation(progShader, "uBarY");
    GLint g_progBarHLoc = glGetUniformLocation(progShader, "uBarH");
    GLint g_progBarWLoc = glGetUniformLocation(progShader, "uBarW");
    GLint g_progBgLoc = glGetUniformLocation(progShader, "uBgColor");
    GLint g_progBorderLoc = glGetUniformLocation(progShader, "uBorderColor");
    auto showLoadingProgress = [&](GLFWwindow* w, const char* msg, float frac) {
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // 画进度条
        glUseProgram(progShader);
        glUniform1f(g_progProgressLoc, frac);
        glUniform1f(g_progBarYLoc, -0.88f);
        glUniform1f(g_progBarHLoc, 0.072f);
        glUniform1f(g_progBarWLoc, 0.94f);
        glUniform3f(g_progBgLoc, 0.15f, 0.15f, 0.18f);
        glUniform3f(g_progBorderLoc, 0.5f, 0.5f, 0.6f);
        glUniform3f(g_progColorLoc, 0.3f, 0.6f, 1.0f);
        glBindVertexArray(progVAO);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        // 文字：FreeType（需要 blending + 无深度测试）
        if (g_textRenderer && msg) {
            float sH = (float)g_screenHeight;
            float textScale = sH / 1080.0f;
            char buf[128];
            int pct = (int)(frac * 100.0f + 0.5f);
            snprintf(buf, sizeof(buf), "%3d%% %s", pct, msg);
            std::string text(buf);
            // 水平居中：精确计算文字宽度
            float tw = g_textRenderer->getTextWidth(text, textScale);
            float tx = ((float)g_screenWidth - tw) * 0.5f;
            // 垂直居中：barCenter = (1-0.88)*0.5*sH = 0.06*sH
            float barCenterPx = (1.0f - 0.88f) * 0.5f * sH;
            float ascender, totalH;
            float ty;
            if (g_textRenderer->getGlyphMetrics(textScale, ascender, totalH))
                ty = barCenterPx + (totalH * 0.5f - ascender);
            else
                ty = barCenterPx;
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            g_textRenderer->renderText(text, tx, ty, textScale, glm::vec3(1.0f));
            glDisable(GL_BLEND);
        }
        glEnable(GL_DEPTH_TEST);
        glfwSwapBuffers(w);
        glfwPollEvents();
    };
    glfwGetFramebufferSize(window, &g_screenWidth, &g_screenHeight);

    // 文字渲染器（FreeType）—— 在进度条之前创建，使第一次显示就有文字
    g_textRenderer = new TextRenderer(g_screenWidth, g_screenHeight);

    // 初始进度 0
    showLoadingProgress(window, "Initializing...", 0.0f);

    // 菜单系统（FreeType）
    g_menu.init(window, g_textRenderer);
    g_menu.setGlobals(&g_bloomEnabled, &g_flareEnabled, &g_autoExposure,
                      nullptr, nullptr, &g_manualExposure, &g_wireframe,
                      &g_resolutionIndex, &g_fullscreen, &g_screenWidth, &g_screenHeight);

    // 应用初始分辨率 & 全屏设置
    g_menu.applyResolution();

    // 着色器（路径前缀来自 config.shaderPath）
    const std::string& sp = config.shaderPath;

    int totalSteps = 43; int step = 0;
    showLoadingProgress(window, "Compiling shader - sun...", (float)(step++) / totalSteps);
    unsigned int sunShader       = createShaderProgramFromFiles((sp + "planet.vert").c_str(), (sp + "sun.frag").c_str());
    showLoadingProgress(window, "Compiling shader - planet...", (float)(step++) / totalSteps);
    unsigned int planetShader    = createShaderProgramFromFiles((sp + "planet.vert").c_str(), (sp + "planet.frag").c_str());
    showLoadingProgress(window, "Compiling shader - lensflare...", (float)(step++) / totalSteps);
    unsigned int lensFlareShader = createShaderProgramFromFiles((sp + "lensflare.vert").c_str(), (sp + "lensflare.frag").c_str());
    showLoadingProgress(window, "Compiling shader - SSAO...", (float)(step++) / totalSteps);
    std::string ssaoSrc      = readShaderFile((sp + "ssao.frag").c_str()); // read-only
    showLoadingProgress(window, "Compiling shader - composite...", (float)(step++) / totalSteps);
    unsigned int ssaoShader      = createShaderProgram(postVertexSrc, ssaoSrc.c_str());
    std::string compositeSrc = readShaderFile((sp + "composite.frag").c_str());
    unsigned int compositeShader = createShaderProgram(postVertexSrc, compositeSrc.c_str());
    showLoadingProgress(window, "Compiling shader - bright...", (float)(step++) / totalSteps);
    unsigned int brightShader    = createShaderProgram(postVertexSrc, brightFragSrc);
    showLoadingProgress(window, "Compiling shader - blur...", (float)(step++) / totalSteps);
    unsigned int blurShader      = createShaderProgram(postVertexSrc, blurFragSrc);
    showLoadingProgress(window, "Compiling shader - final...", (float)(step++) / totalSteps);
    std::string finalSrc = readShaderFile((sp + "final.frag").c_str());
    unsigned int finalShader     = createShaderProgram(postVertexSrc, finalSrc.c_str());
    showLoadingProgress(window, "Compiling shader - atmosphere...", (float)(step++) / totalSteps);
    unsigned int redShader       = createShaderProgram(redVertSrc, redFragSrc);
    unsigned int atmosphereShader = createShaderProgramFromFiles((sp + "atmosphere.vert").c_str(), (sp + "atmosphere.frag").c_str());
    showLoadingProgress(window, "Compiling shader - planet glow...", (float)(step++) / totalSteps);
    unsigned int planetGlowShader = createShaderProgramFromFiles((sp + "planet_glow.vert").c_str(), (sp + "planet_glow.frag").c_str());


    if (!sunShader || !planetShader || !lensFlareShader || !ssaoShader || !compositeShader ||
        !brightShader || !blurShader || !finalShader || !redShader || !atmosphereShader ||
        !planetGlowShader) {
        std::cerr << "Shader compilation failed!" << std::endl;
        return -1;
    }

    createQuad();
    if (!createGBuffer(g_screenWidth, g_screenHeight)) return -1;
    createSSAOFBO(g_screenWidth, g_screenHeight);
    createSceneFBO(g_screenWidth, g_screenHeight);
    createSunEmissiveFBO(g_screenWidth, g_screenHeight);
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
    showLoadingProgress(window, "Compiling shader - luminance...", (float)(step++) / totalSteps);
    std::string lumFragSrc = readShaderFile((sp + "luminance.frag").c_str());
    lumShader = createShaderProgram(postVertexSrc, lumFragSrc.c_str());
    if (!lumShader) { std::cerr << "Luminance shader failed!" << std::endl; return -1; }

    SphereLOD sphereLOD = createSphereLOD();
    Camera camera(config.cameraPos, config.cameraYaw, config.cameraPitch, config.cameraRoll,
                  config.cameraFov, config.cameraSpeed, config.cameraSensitivity);
    g_camera = &camera;

    std::vector<Planet> planets;
    for (auto& p : config.planets) {
        char pmsg[64]; snprintf(pmsg, sizeof(pmsg), "Loading texture - %s...", p.name.c_str());
        showLoadingProgress(window, pmsg, (float)(step++) / totalSteps);
        unsigned int tex = loadTexture(p.texturePath, p.color);
        bool atm = (p.name == "earth" || p.name == "venus" || p.name == "mars");
        planets.emplace_back(p.name, p.orbitRadius, p.orbitPeriod, p.rotationPeriod,
                             p.size, tex, p.color, p.roughness, p.metallic, atm);
    }

    // 计算 J2000.0 (JD 2451545.0) 到当前时刻的天数
    {
        time_t now = time(nullptr);
        double jd = now / 86400.0 + 2440587.5;        // Unix epoch → JD
        double daysSinceJ2000 = jd - 2451545.0; // JD → J2000.0 offset
        Planet::setEpochDays(daysSinceJ2000);
    }

    // 设置每颗行星的轨道力学参数
    for (size_t i = 0; i < config.planets.size() && i < planets.size(); i++) {
        auto& pc = config.planets[i];
        planets[i].setOrbitalElements(
            pc.inclination, pc.eccentricity,
            pc.longitudeAscendingNode, pc.argumentOfPeriapsis,
            pc.meanAnomalyAtEpoch, pc.axialTilt);
    }

    // ========== 卫星 (从 config.ini 读取) ==========
    auto findPlanet = [&](const std::string& name) -> Planet* {
        for (auto& p : planets) if (p.getName() == name) return &p;
        return nullptr;
    };

    for (const auto& mc : config.moons) {
        Planet* parent = findPlanet(mc.parent);
        if (!parent) {
            std::cerr << "WARNING: Moon '" << mc.name << "' parent '" << mc.parent << "' not found" << std::endl;
            continue;
        }
        char mmsg[64]; snprintf(mmsg, sizeof(mmsg), "Loading texture - %s...", mc.name.c_str());
        showLoadingProgress(window, mmsg, (float)(step++) / totalSteps);
        unsigned int tex = loadTexture(mc.texturePath, mc.color);
        parent->addMoon(mc.name, mc.orbitRadius, mc.orbitPeriod,
                        mc.meanAnomalyAtEpoch, mc.size, mc.color, tex);
    }

    int totalMoons = 0;
    for (auto& p : planets) totalMoons += (int)p.getMoons().size();
    // 卫星光照用 fallback 白色纹理
    unsigned int whiteTex;
    {
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &whiteTex);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    // 点精灵 VAO (用于 sub-pixel 天体)
    {
        glGenVertexArrays(1, &pointVAO);
        glGenBuffers(1, &pointVBO);
        float pointVert[3] = {0.0f, 0.0f, 0.0f};
        glBindVertexArray(pointVAO);
        glBindBuffer(GL_ARRAY_BUFFER, pointVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pointVert), pointVert, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    glm::dvec3 sunPos(0.0);
    float sunRadius = 15.0f;
    if (!planets.empty()) { sunPos = planets[0].getPosition(); sunRadius = planets[0].getSize(); }

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

    float lightIntensity = 5000.0f;   // 线性衰减 1/dist，地球 ≈ 5.0
    float flareIntensity = 0.6f;
    float ssaoStrength = 0.8f;
    float coronaIntensity = 0.5f;
    float kSunRadius = 15.0f;  // 与 config.ini 同步，用于角度计算

    bool escapePressedLast = false;

    float fpsAccum = 0.0f;
    int   fpsCount = 0;

    g_lastFrame = (float)glfwGetTime();  // 避免首帧 deltaTime 异常大

    while (!glfwWindowShouldClose(window)) {
        float currentTime = glfwGetTime();
        g_deltaTime = currentTime - g_lastFrame;
        g_lastFrame = currentTime;

        fpsCount++;
        fpsAccum += g_deltaTime;
        if (fpsAccum >= 0.5f) {
            int fps = (int)(fpsCount / fpsAccum + 0.5f);
            g_lastFps = fps;
            fpsAccum = 0.0f;
            fpsCount = 0;
        }

        // FBO 重建（分辨率变更后）
        if (g_fboDirty) {
            if (recreateFBOs(g_screenWidth, g_screenHeight)) {
                g_fboDirty = false;
            } else {
                std::cerr << "[ERROR] FBO recreation failed!" << std::endl;
            }
        }

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapePressedLast) {
            if (g_menu.isGameplay())
                g_menu.togglePause();
            else
                g_menu.back();
        }
        escapePressedLast = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);

        if (g_menu.isGameplay()) {
            camera.processKeyboard(GLFW_KEY_W, glfwGetKey(window, GLFW_KEY_W), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_A, glfwGetKey(window, GLFW_KEY_A), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_S, glfwGetKey(window, GLFW_KEY_S), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_D, glfwGetKey(window, GLFW_KEY_D), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_Q, glfwGetKey(window, GLFW_KEY_Q), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_E, glfwGetKey(window, GLFW_KEY_E), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_R, glfwGetKey(window, GLFW_KEY_R), g_deltaTime);
            camera.processKeyboard(GLFW_KEY_F, glfwGetKey(window, GLFW_KEY_F), g_deltaTime);
            // 碰撞边界必须在 update() 之前设置，才能过滤位移
            std::vector<CollisionSphere> collisionSpheres;
            for (auto& p : planets) {
                collisionSpheres.push_back({p.getPosition(), p.getSize() * 1.002});
                for (auto& m : p.getMoons()) {
                    collisionSpheres.push_back({m.worldPosition, m.size * 1.002});
                }
            }
            camera.setCollisionBoundaries(collisionSpheres, 1.002);
            camera.update(g_deltaTime);
        }

        // 累积式时间推进：simTime += deltaTime * timeScale，改流速不影响位置
        g_simTime += g_deltaTime * config.timeScale;
        for (auto& p : planets) p.update(g_simTime);

        glm::dvec3 viewPosD = camera.getPosition();
        glm::vec3 viewPos = glm::vec3(viewPosD);
        glm::dmat4 viewD = camera.getViewMatrix();          // 双精度，用于太阳位置等关键计算
        glm::mat4 view   = glm::mat4(viewD);                 // 单精度，供 OpenGL 渲染
        glm::mat4 viewRot = glm::mat4(glm::mat3(view));  // 仅旋转部分，配合相对坐标 model 使用

        // dynamic near plane: 近平面 = 到最近表面距离 × 0.5，实时调整
        double targetNear = 0.1;
        double minDistToSurface = 1e10;
        double nearestBodyRadius = sunRadius;
        for (auto& p : planets) {
            double distToSurface = glm::length(p.getPosition() - viewPosD) - (double)p.getSize();
            if (distToSurface < 0.0) distToSurface = 1e-5;
            if (distToSurface < minDistToSurface) minDistToSurface = distToSurface;
            double nearForThis = distToSurface * 0.5;
            if (nearForThis < targetNear) targetNear = nearForThis;
            if (p.getSize() < nearestBodyRadius) nearestBodyRadius = p.getSize();
            for (auto& m : p.getMoons()) {
                double moonDistToSurface = glm::length(m.worldPosition - viewPosD) - (double)m.size;
                if (moonDistToSurface < 0.0) moonDistToSurface = 1e-5;
                if (moonDistToSurface < minDistToSurface) minDistToSurface = moonDistToSurface;
                double moonNear = moonDistToSurface * 0.5;
                if (moonNear < targetNear) targetNear = moonNear;
                if (m.size < nearestBodyRadius) nearestBodyRadius = m.size;
            }
        }
        // 下界: nearestBodyRadius * 0.001，但自适应缩放 ——
        // minDistToSurface * 1e-4 确保近平面永不超过到最近表面距离的万分之一，
        // 防止 tiny 天体（火卫一 size=0.00056）在相机贴脸时被近平面整体裁掉
        double absFloor = glm::max(nearestBodyRadius * 1e-3, minDistToSurface * 1e-4);
        if (targetNear < absFloor) targetNear = absFloor;
        float dynamicNear = (float)targetNear;

        // dynamic far plane: 根据到最近表面的距离调整，下界 20000，上界 200000
        const float BASE_FAR = 20000.0f;
        const float MAX_FAR  = 200000.0f;
        float dynamicFar = glm::clamp((float)minDistToSurface * 10.0f, BASE_FAR, MAX_FAR);

        float aspect = (g_screenHeight > 0) ? (float)g_screenWidth / g_screenHeight : 1.0f;
        glm::mat4 proj = glm::perspective(glm::radians(camera.getFov()),
                                          aspect,
                                          dynamicNear, dynamicFar);

        // ================================================================
        // Pass 1: G-Buffer 渲染 (MRT: color + normal + depth)
        // ================================================================
        glBindFramebuffer(GL_FRAMEBUFFER, gBufferFBO);
        GLenum gBufDraw[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, gBufDraw);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        checkGLError("G-Buffer clear");

        // 太阳：纯 emissive，写 G-Buffer（太阳始终用最高精度 LOD0）
        {
            auto lod = sphereLOD.levels[0];
            for (auto& p : planets) {
                if (p.isSun()) {
                    p.drawEmissive(sunShader, lod.VAO, lod.indexCount, view, viewRot, proj, viewPosD, 10.0f);
                }
            }
        }
        // 诊断：线框模式
        if (g_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        // 行星：从太阳接收光照，每行星按距离动态选 LOD
        for (auto& p : planets) {
            if (!p.isSun()) {
                float dist = glm::length(glm::vec3(p.getPosition() - viewPosD));
                auto lod = selectLOD(sphereLOD, dist, p.getSize(), config.cameraFov, g_screenHeight);
                p.draw(planetShader, lod.VAO, lod.indexCount, view, viewRot, proj, viewPosD, lightIntensity, sunRadius);
            }
        }
        checkGLError("planet draw");

        // ===== 卫星渲染 (G-Buffer, PBR光照同行星) =====
        glUseProgram(planetShader);
        glUniformMatrix4fv(glGetUniformLocation(planetShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(planetShader, "viewRot"), 1, GL_FALSE, glm::value_ptr(viewRot));
        glUniformMatrix4fv(glGetUniformLocation(planetShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
        // sunRelative = sunCenter - cameraPos（double 精度 CPU 计算 → vec3）
        glUniform3fv(glGetUniformLocation(planetShader, "sunRelative"), 1,
                     glm::value_ptr(glm::vec3(sunPos - viewPosD)));
        glUniform1f(glGetUniformLocation(planetShader, "lightIntensity"), lightIntensity);
        glUniform1f(glGetUniformLocation(planetShader, "sunRadius"), sunRadius);
        glUniform1f(glGetUniformLocation(planetShader, "roughness"), 0.8f);
        glUniform1f(glGetUniformLocation(planetShader, "metallic"), 0.0f);
        glUniform1i(glGetUniformLocation(planetShader, "hasAtmosphere"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, whiteTex);
        glUniform1i(glGetUniformLocation(planetShader, "textureSampler"), 0);
        unsigned int lastMoonTex = whiteTex;
        for (auto& p : planets) {
            for (auto& m : p.getMoons()) {
                // 卫星纹理绑定
                unsigned int moonTex = m.textureID ? m.textureID : whiteTex;
                if (moonTex != lastMoonTex) {
                    glBindTexture(GL_TEXTURE_2D, moonTex);
                    lastMoonTex = moonTex;
                }
                glUniform3fv(glGetUniformLocation(planetShader, "uBaseColor"), 1,
                             m.textureID ? glm::value_ptr(glm::vec3(1.0f)) : glm::value_ptr(m.color));
                // 行星遮挡投影检测 (ray-sphere)
                float shadowFactor = 1.0f;
                {
                    glm::dvec3 toSun = sunPos - (glm::dvec3)m.worldPosition;
                    double distToSun = glm::length(toSun);
                    glm::dvec3 sunDir = toSun / distToSun;
                    for (auto& occluder : planets) {
                        if (occluder.isSun()) continue;
                        glm::dvec3 toPlanet = occluder.getPosition() - (glm::dvec3)m.worldPosition;
                        double t = glm::dot(toPlanet, sunDir);
                        if (t < 0.0 || t > distToSun) continue;
                        double d2 = glm::dot(toPlanet, toPlanet) - t * t;
                        double r = (double)occluder.getSize();
                        if (d2 < r * r) { shadowFactor = 0.0f; break; }
                    }
                }
                // 判断是否为 sub-pixel：屏幕空间半径 < 0.5 像素
                float dist = glm::length(glm::vec3(m.worldPosition - viewPosD));
                float angularRadius = atan(m.size / dist);
                float fovy = glm::radians(config.cameraFov);
                float pixelRadius = angularRadius * g_screenHeight / (2.0f * tan(fovy / 2.0f));

                glUniform1f(glGetUniformLocation(planetShader, "uShadowFactor"), shadowFactor);
                if (pixelRadius < 0.5f && !g_wireframe) {
                    // Sub-pixel：用 GL_POINTS 渲染为 1 像素点
                    glPointSize(1.0f);
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m.worldPosition));
                    glUniformMatrix4fv(glGetUniformLocation(planetShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glBindVertexArray(pointVAO);
                    glDrawArrays(GL_POINTS, 0, 1);
                } else {
                    // 正常球体渲染 - 相机相对坐标避免 float 精度丢失
                    auto lod = selectLOD(sphereLOD, dist, m.size, config.cameraFov, g_screenHeight);
                    // 对很小的天体（size < 0.01），强制用最高精度
                    if (m.size < 0.01f) {
                        lod = { sphereLOD.levels[0].VAO, sphereLOD.levels[0].indexCount };
                    }
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m.worldPosition - viewPosD));
                    model = glm::scale(model, glm::vec3(m.size));
                    glUniformMatrix4fv(glGetUniformLocation(planetShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glBindVertexArray(lod.VAO);
                    glDrawElements(GL_TRIANGLES, lod.indexCount, GL_UNSIGNED_INT, 0);
                }
            }
        }
        glBindVertexArray(0);
        if (g_wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        // ===== 大气散射 (Rayleigh + Mie) - additive 叠加 =====
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
        glUseProgram(atmosphereShader);
        for (auto& p : planets) {
            if (p.hasAtmosphere()) {
                float pSize = p.getSize();
                // Per-planet atmosphere shell size
                float atmosScale = 1.15f;
                if (p.getName() == "venus")  atmosScale = 1.20f;
                if (p.getName() == "mars")   atmosScale = 1.10f;
                float atmosR = pSize * atmosScale;
                glm::mat4 atmosM = glm::translate(glm::mat4(1.0f), glm::vec3(p.getPosition() - viewPosD));
                atmosM = glm::scale(atmosM, glm::vec3(atmosR));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "model"), 1, GL_FALSE, glm::value_ptr(atmosM));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "viewRot"), 1, GL_FALSE, glm::value_ptr(viewRot));
                glUniformMatrix4fv(glGetUniformLocation(atmosphereShader, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "cameraPos"), 1, glm::value_ptr(viewPos));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "planetCenter"), 1, glm::value_ptr(glm::vec3(p.getPosition())));
                glUniform1f(glGetUniformLocation(atmosphereShader, "planetRadius"), pSize);
                glUniform1f(glGetUniformLocation(atmosphereShader, "atmosphereRadius"), atmosR);
                glm::vec3 sunDN = glm::normalize(glm::vec3(sunPos - p.getPosition()));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "sunDir"), 1, glm::value_ptr(sunDN));
                // Per-planet atmosphere scattering parameters
                glm::vec3 rayleighCol, mieCol;
                float densityFO, rayleighStr, mieStr;
                if (p.getName() == "venus") {
                    // Venus: thick CO2, whitish-yellow haze, Mie dominant
                    rayleighCol = glm::vec3(0.5f, 0.6f, 0.7f);
                    mieCol      = glm::vec3(1.0f, 0.95f, 0.8f);
                    densityFO   = 0.6f;
                    rayleighStr = 0.15f;
                    mieStr      = 0.6f;
                } else if (p.getName() == "mars") {
                    // Mars: thin CO2 + dust, reddish-orange limb
                    rayleighCol = glm::vec3(0.3f, 0.5f, 1.0f);
                    mieCol      = glm::vec3(1.0f, 0.5f, 0.3f);
                    densityFO   = 1.5f;
                    rayleighStr = 0.35f;
                    mieStr      = 0.5f;
                } else {
                    // Earth: default blue Rayleigh + white Mie
                    rayleighCol = glm::vec3(0.25f, 0.55f, 1.0f);
                    mieCol      = glm::vec3(1.0f, 0.90f, 0.65f);
                    densityFO   = 1.0f;
                    rayleighStr = 0.7f;
                    mieStr      = 0.35f;
                }
                glUniform3fv(glGetUniformLocation(atmosphereShader, "uRayleighColor"),  1, glm::value_ptr(rayleighCol));
                glUniform3fv(glGetUniformLocation(atmosphereShader, "uMieColor"),      1, glm::value_ptr(mieCol));
                glUniform1f(glGetUniformLocation(atmosphereShader, "uDensityFalloff"),  densityFO);
                glUniform1f(glGetUniformLocation(atmosphereShader, "uRayleighStrength"), rayleighStr);
                glUniform1f(glGetUniformLocation(atmosphereShader, "uMieStrength"),     mieStr);
                float dist = glm::length(glm::vec3(p.getPosition() - viewPosD));
                auto lod = selectLOD(sphereLOD, dist, atmosR, config.cameraFov, g_screenHeight);
                glBindVertexArray(lod.VAO);
                glDrawElements(GL_TRIANGLES, lod.indexCount, GL_UNSIGNED_INT, 0);
            }
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        checkGLError("atmosphere pass");

        // ================================================================
        // Pass 1.5: Sun Emissive → 独立 FBO（深度测试隔离行星遮挡）
        // ================================================================
        {
            // Blit G-buffer 深度到 sunEmissiveFBO
            glBindFramebuffer(GL_READ_FRAMEBUFFER, gBufferFBO);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sunEmissiveFBO);
            glBlitFramebuffer(0, 0, g_screenWidth, g_screenHeight,
                              0, 0, g_screenWidth, g_screenHeight,
                              GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, sunEmissiveFBO);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            // 深度测试启用，深度写入禁（用 G-buffer 已有的深度）
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            // 渲染太阳 emissive - 被行星遮挡的像素自动不写入
            {
                auto lod = sphereLOD.levels[0];
                for (auto& p : planets) {
                    if (p.isSun()) {
                        p.drawEmissive(sunShader, lod.VAO, lod.indexCount, view, viewRot, proj, viewPosD, 10.0f);
                    }
                }
            }
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);   // 恢复默认深度比较函数
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            checkGLError("sun emissive FBO");
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
        glUniform1f(glGetUniformLocation(ssaoShader, "uTime"), currentTime);
        glUniform1f(glGetUniformLocation(ssaoShader, "uNearPlane"), dynamicNear);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        checkGLError("SSAO pass");

        // ================================================================
        // Pass 3: 合成 (scene * SSAO + corona + 日食) → sceneFBO
        // ================================================================
        // 计算太阳屏幕坐标（供 composite shader 和后续 lens flare 使用）
        // 用双精度矩阵避免 far=80000+ 时 float 精度导致太阳位置在屏幕格点间跳跃
        float sunScreenX = -1.0f, sunScreenY = -1.0f;
        float sunUVRadius = 0.05f;
        bool sunDiskOnScreen = false;
        {
            double aspectD = (g_screenHeight > 0) ? (double)g_screenWidth / g_screenHeight : 1.0;
            glm::dmat4 projD = glm::perspective(glm::radians((double)camera.getFov()),
                                                aspectD,
                                                (double)dynamicNear, (double)dynamicFar);
            glm::dvec4 sunClip = projD * viewD * glm::dvec4(sunPos.x, sunPos.y, sunPos.z, 1.0);
            if (sunClip.w > 0.0) {
                glm::dvec3 ndc = glm::dvec3(sunClip) / sunClip.w;
                double distToSun = glm::length(glm::dvec3(sunPos) - viewPosD);
                double sunAngRadius = glm::atan((double)kSunRadius / distToSun);
                double fovY = glm::radians((double)camera.getFov());
                double sunNDCRadius = glm::tan(sunAngRadius) / glm::tan(fovY * 0.5);
                sunDiskOnScreen = (ndc.x + sunNDCRadius > -1.0 && ndc.x - sunNDCRadius < 1.0 &&
                                    ndc.y + sunNDCRadius > -1.0 && ndc.y - sunNDCRadius < 1.0);
                sunScreenX = (float)((ndc.x + 1.0) * 0.5);
                sunScreenY = (float)((ndc.y + 1.0) * 0.5);
                sunUVRadius = (float)(sunNDCRadius * 0.5);
                if (sunUVRadius < 0.0005f) sunUVRadius = 0.0005f;
            }
        }

        // 读取太阳 disc 区域 emissive 亮度 → 区域平均 → EMA 平滑 → lensflare 稳定
        // 区域平均替代单像素读取：当太阳 disc < 3px 时避免采样值在 0/半亮/全亮 间跳跃
        if (sunDiskOnScreen && g_flareEnabled) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, sunEmissiveFBO);
            // 采样区域半径（像素）：至少 2px，确保覆盖整个 disc + 边缘
            int sampleR = glm::max(2, (int)(sunUVRadius * g_screenWidth + 0.5f));
            int cx = glm::clamp((int)(sunScreenX * g_screenWidth), 0, g_screenWidth - 1);
            int cy = glm::clamp((int)(sunScreenY * g_screenHeight), 0, g_screenHeight - 1);
            int x0 = glm::max(0, cx - sampleR);
            int y0 = glm::max(0, cy - sampleR);
            int x1 = glm::min(g_screenWidth - 1, cx + sampleR);
            int y1 = glm::min(g_screenHeight - 1, cy + sampleR);
            int w = x1 - x0 + 1;
            int h = y1 - y0 + 1;
            float rawEm = 0.0f;
            if (w > 0 && h > 0) {
                std::vector<float> block(w * h * 4);
                glReadPixels(x0, y0, w, h, GL_RGBA, GL_FLOAT, block.data());
                float sum = 0.0f;
                for (int i = 0; i < w * h; i++) {
                    float mx = fmaxf(fmaxf(block[i*4], block[i*4+1]), block[i*4+2]);
                    sum += mx;
                }
                rawEm = sum / (float)(w * h);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            float alpha = glm::clamp(g_deltaTime * 5.0f, 0.0f, 1.0f);
            g_smoothedEmissive += (rawEm - g_smoothedEmissive) * alpha;
        }

        // === 太阳 disc 可见中心 & 面积比例 ===
        // 屏幕裁剪后的可见矩形（UV空间 [0,1]）
        float discLeft   = glm::clamp(sunScreenX - sunUVRadius, 0.0f, 1.0f);
        float discRight  = glm::clamp(sunScreenX + sunUVRadius, 0.0f, 1.0f);
        float discBottom = glm::clamp(sunScreenY - sunUVRadius, 0.0f, 1.0f);
        float discTop    = glm::clamp(sunScreenY + sunUVRadius, 0.0f, 1.0f);

        // 可见中心：太阳 disc 屏幕内部分的几何中心
        float flareCenterX = (discLeft + discRight) * 0.5f;
        float flareCenterY = (discBottom + discTop) * 0.5f;

        // 屏幕可见比例（近似：可见矩形面积 / 外接正方形面积）
        float visWidth  = discRight  - discLeft;
        float visHeight = discTop - discBottom;
        float screenFrac = (sunUVRadius > 0.0001f)
            ? glm::clamp((visWidth * visHeight) / (sunUVRadius * sunUVRadius * 4.0f), 0.0f, 1.0f)
            : 0.0f;

        // 行星遮挡面积比例（圆-圆重叠精确计算）
        float sunDiscVisible = sunDiskOnScreen ? 1.0f : 0.0f;
        if (sunDiscVisible > 0.01f && sunUVRadius > 0.0f) {
            float sunArea = glm::pi<float>() * sunUVRadius * sunUVRadius;
            float totalOccluded = 0.0f;
            struct { float uvX, uvY, uvR; } discs[64];
            int discCount = 0;
            float fovY = glm::radians(camera.getFov());
            for (auto& p : planets) {
                if (p.isSun() || discCount >= 64) continue;
                glm::dvec3 pp = p.getPosition();
                glm::dvec3 ts = sunPos - pp;
                glm::dvec3 tc = viewPosD - pp;
                if (glm::dot(ts, tc) >= 0.0) continue;
                glm::vec4 cl = proj * view * glm::vec4(glm::vec3(pp), 1.0f);
                if (cl.w <= 0.0f) continue;
                glm::vec3 nd = glm::vec3(cl) / cl.w;
                float dp = glm::length(glm::vec3(pp - viewPosD));
                float bs = p.hasAtmosphere() ? p.getSize() * 1.15f : p.getSize();
                float ar = glm::atan(bs / dp);
                float nr = glm::tan(ar) / glm::tan(fovY * 0.5f);
                discs[discCount++] = {(nd.x + 1.0f) * 0.5f, (nd.y + 1.0f) * 0.5f, nr * 0.5f};
            }
            for (auto& p : planets) {
                for (auto& m : p.getMoons()) {
                    if (discCount >= 64) break;
                    glm::dvec3 mp = m.worldPosition;
                    glm::dvec3 ts = sunPos - mp;
                    glm::dvec3 tc = viewPosD - mp;
                    if (glm::dot(ts, tc) >= 0.0) continue;
                    glm::vec4 cl = proj * view * glm::vec4(glm::vec3(mp), 1.0f);
                    if (cl.w <= 0.0f) continue;
                    glm::vec3 nd = glm::vec3(cl) / cl.w;
                    float dm = glm::length(glm::vec3(mp - viewPosD));
                    float ar = glm::atan(m.size / dm);
                    float nr = glm::tan(ar) / glm::tan(fovY * 0.5f);
                    discs[discCount++] = {(nd.x + 1.0f) * 0.5f, (nd.y + 1.0f) * 0.5f, nr * 0.5f};
                }
            }
            float aspect = (float)g_screenWidth / g_screenHeight;
            for (int i = 0; i < discCount; i++) {
                float dx = (discs[i].uvX - sunScreenX) * aspect;
                float dy = discs[i].uvY - sunScreenY;
                float d = sqrt(dx * dx + dy * dy);
                float rP = discs[i].uvR;
                float rS = sunUVRadius;
                if (d >= rS + rP) continue;
                if (d <= fabs(rS - rP)) {
                    if (rP >= rS) { totalOccluded = sunArea; break; }
                    totalOccluded += glm::pi<float>() * rP * rP;
                    continue;
                }
                float d1 = (rS * rS - rP * rP + d * d) / (2.0f * d);
                float d2 = d - d1;
                float ol = rS * rS * (float)acos(d1 / rS) - d1 * sqrt(rS * rS - d1 * d1)
                         + rP * rP * (float)acos(d2 / rP) - d2 * sqrt(rP * rP - d2 * d2);
                totalOccluded += ol;
            }
            sunDiscVisible = glm::clamp(1.0f - totalOccluded / sunArea, 0.0f, 1.0f);
        }

        // 综合可见比例：屏幕裁剪 × 行星遮挡
        float sunVisibleCombined = sunDiscVisible * screenFrac;

        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(compositeShader);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gColorTex);
        glUniform1i(glGetUniformLocation(compositeShader, "gColor"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ssaoTex);
        glUniform1i(glGetUniformLocation(compositeShader, "ssaoTex"), 1);

        // 日冕强度：按太阳 disc 屏幕内可见比例缩放（基于可见中心，不是太阳中心NDC）
        float actualCoronaIntensity = coronaIntensity * screenFrac;
        float ssaoDistanceFactor = glm::smoothstep(0.5f, 3.0f, (float)minDistToSurface);
        float effectiveSSAO = ssaoStrength * ssaoDistanceFactor;
        glUniform1f(glGetUniformLocation(compositeShader, "uSSAOStrength"), effectiveSSAO);
        glUniform2f(glGetUniformLocation(compositeShader, "uSunPos"), sunScreenX, sunScreenY);
        glUniform1f(glGetUniformLocation(compositeShader, "uSunRadius"), sunUVRadius);
        glUniform1f(glGetUniformLocation(compositeShader, "uIntensity"), actualCoronaIntensity);
        glUniform1f(glGetUniformLocation(compositeShader, "uAspectRatio"), (float)g_screenWidth / g_screenHeight);
        glUniform3f(glGetUniformLocation(compositeShader, "uSunColor"), 1.0f, 0.85f, 0.2f);
        glUniform1f(glGetUniformLocation(compositeShader, "uSunDiscVisible"), sunDiscVisible);

        // 传入遮挡行星数据（所有屏上可见行星，用于 corona 几何遮挡 + rim glow）
        {
            float fovY = glm::radians(camera.getFov());
            int eclipseCount = 0;
            GLint countLoc = glGetUniformLocation(compositeShader, "uEclipseCount");
            GLint centerLoc = glGetUniformLocation(compositeShader, "uEclipseCenter[0]");
            GLint radiusLoc = glGetUniformLocation(compositeShader, "uEclipseRadius[0]");
            GLint rimGlowLoc = glGetUniformLocation(compositeShader, "uEclipseRimGlow[0]");

            for (auto& p : planets) {
                if (p.isSun()) continue;
                if (eclipseCount >= 32) break;
                glm::dvec3 planetPos = p.getPosition();
                glm::dvec3 toSun    = sunPos - planetPos;
                glm::dvec3 toCamera = viewPosD - planetPos;
                glm::vec4 clip = proj * view * glm::vec4(glm::vec3(planetPos), 1.0f);
                if (clip.w <= 0.0f) continue;
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                if (abs(ndc.x) > 1.2f || abs(ndc.y) > 1.2f) continue;  // 屏外跳过
                float distToPlanet = glm::length(glm::vec3(planetPos - viewPosD));
                float bodySize = p.hasAtmosphere() ? p.getSize() * 1.15f : p.getSize();
                float angRadius = glm::atan(bodySize / distToPlanet);
                float ndcRadius = glm::tan(angRadius) / glm::tan(fovY * 0.5f);
                glUniform2f(centerLoc + eclipseCount, ndc.x, ndc.y);
                glUniform1f(radiusLoc + eclipseCount, ndcRadius);
                // rim glow 仅大气凌日行星：太阳在行星背后 + 行星有大气层
                bool isTransit = (glm::dot(toSun, toCamera) < 0.0);
                glUniform1i(rimGlowLoc + eclipseCount, (isTransit && p.hasAtmosphere()) ? 1 : 0);
                eclipseCount++;
            }
            // 卫星也加入日冕遮挡（无大气层，无 rim glow）
            for (auto& p : planets) {
                for (auto& m : p.getMoons()) {
                    if (eclipseCount >= 32) break;
                    glm::dvec3 moonPos = m.worldPosition;
                    glm::vec4 clip = proj * view * glm::vec4(glm::vec3(moonPos), 1.0f);
                    if (clip.w <= 0.0f) continue;
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    if (abs(ndc.x) > 1.2f || abs(ndc.y) > 1.2f) continue;
                    float distToMoon = glm::length(glm::vec3(moonPos - viewPosD));
                    float angRadius = glm::atan(m.size / distToMoon);
                    float ndcRadius = glm::tan(angRadius) / glm::tan(fovY * 0.5f);
                    ndcRadius *= 1.08f;
                    glUniform2f(centerLoc + eclipseCount, ndc.x, ndc.y);
                    glUniform1f(radiusLoc + eclipseCount, ndcRadius);
                    glUniform1i(rimGlowLoc + eclipseCount, 0);  // 卫星无 rim glow
                    eclipseCount++;
                }
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
            glFlush();  // 确保 GPU 完成 luminance chain 渲染，再读回
            float rawLogLum;
            glReadPixels(0, 0, 1, 1, GL_RED, GL_FLOAT, &rawLogLum);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

            // 对 rawLogLum 做微量平滑，防止 1x1 下采样噪声导致逐帧抖动
            static float smoothRawLogLum = rawLogLum;
            smoothRawLogLum += (rawLogLum - smoothRawLogLum) * 0.6f;

            float delta = smoothRawLogLum - g_adaptedLogLum;
            // 快速适应（约0.3秒追上 10× 亮度变化），适应慢 → 拖影，适应快 → 闪烁
            float adaptSpeed = (delta > 0.0f) ? 4.0f : 8.0f;
            float t = glm::min(adaptSpeed * g_deltaTime, 1.0f);
            delta *= t;
            // 放宽每帧步进上限（原 0.08 太保守，场景亮度快速变化时跟不上）
            float maxDelta = 0.5f;
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
        glUniform1f(glGetUniformLocation(brightShader, "threshold"), 2.0f);
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

                glm::dvec3 planetPos = p.getPosition();
                glm::vec4 clip = proj * view * glm::vec4(glm::vec3(planetPos), 1.0f);
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
                float distToPlanet = glm::length(glm::vec3(planetPos - viewPosD));
                float angRadius = glm::atan(p.getSize() / distToPlanet);
                float ndcRadius = glm::tan(angRadius) / glm::tan(fovY * 0.5f);
                float pixelRadius = ndcRadius * 0.5f * screenHeight;

                // 暗面检测：相机在行星暗面时不渲染辉光
                glm::dvec3 toSun    = sunPos - planetPos;
                glm::dvec3 toCamera = viewPosD - planetPos;
                if (glm::dot(toSun, toCamera) < 0.0) continue;

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
            // sunVisibleCombined 已传给 lens flare shader

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(lensFlareShader);
            checkGLError("lens flare useProgram");
            glUniform2f(glGetUniformLocation(lensFlareShader, "uSunPos"), flareCenterX, flareCenterY);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uSunVisible"), sunVisibleCombined);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uSunRadius"), sunUVRadius);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uIntensity"), flareIntensity);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uAverageEmissive"), g_smoothedEmissive);
            glUniform1f(glGetUniformLocation(lensFlareShader, "uAspectRatio"), (float)g_screenWidth / g_screenHeight);
            // 绑定太阳 emissive texture（只有露出来的部分有亮度）
            glActiveTexture(GL_TEXTURE0 + 8);
            glBindTexture(GL_TEXTURE_2D, sunEmissiveTex);
            glUniform1i(glGetUniformLocation(lensFlareShader, "uSunEmissive"), 8);
            checkGLError("lens flare uSunEmissive");

            checkGLError("lens flare uniforms");
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            checkGLError("lens flare drawArrays");
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // 屏幕右上角渲染 FPS（FreeType）
        if (g_textRenderer && g_menu.isGameplay()) {
            char fpsStr[32];
            snprintf(fpsStr, sizeof(fpsStr), "FPS: %d", g_lastFps);
            int textWidth = (int)strlen(fpsStr) * 14;
            float x = (float)(g_screenWidth - textWidth - 20);
            float y = (float)(g_screenHeight - 30);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            g_textRenderer->renderText(fpsStr, x, y, 1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }

        // 菜单渲染（FreeType，标题画面/暂停/设置）
        g_menu.render(g_screenWidth, g_screenHeight);

        // 退出检查
        if (g_menu.isQuit()) glfwSetWindowShouldClose(window, true);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 退出前把运行时状态写回 config
    config.bloomEnabled   = g_bloomEnabled;
    config.flareEnabled   = g_flareEnabled;
    config.autoExposure   = g_autoExposure;
    config.wireframe      = g_wireframe;
    config.manualExposure = g_manualExposure;
    config.resolutionIndex = g_resolutionIndex;
    config.fullscreen      = g_fullscreen;

    // 相机状态
    config.cameraPos        = camera.getPosition();
    config.cameraYaw       = camera.getYaw();
    config.cameraPitch     = camera.getPitch();
    config.cameraRoll      = camera.getRoll();
    config.cameraFov       = camera.getFov();
    config.cameraSpeed     = camera.getSpeed();
    config.cameraSensitivity = camera.getSensitivity();

    config.save("config.ini");

    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);
    deleteSphereLOD(sphereLOD);
    delete g_textRenderer; g_textRenderer = nullptr;
    glDeleteProgram(sunShader);
    glDeleteProgram(planetShader);
    glDeleteProgram(lensFlareShader);
    glDeleteProgram(ssaoShader);
    glDeleteProgram(compositeShader);
    glDeleteProgram(brightShader);
    glDeleteProgram(blurShader);
    glDeleteProgram(finalShader);
    glDeleteProgram(redShader);
    glDeleteProgram(atmosphereShader);
    glDeleteProgram(planetGlowShader);
    glfwTerminate();
    return 0;
}
