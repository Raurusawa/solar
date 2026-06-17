#include "menu.h"
#include "text_renderer.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <cstring>
#include <glm/glm.hpp>

// 来自 main.cpp：切回 gameplay 时重置 firstMouse，避免视角跳动
extern bool g_firstMouse;

// 分辨率列表（按纵横比分组的常见比例）
static const int RESOLUTIONS[][2] = {
    // 4:3
    { 800,  600},
    {1024,  768},
    {1280,  960},
    {1600, 1200},
    // 16:10
    {1280,  800},
    {1440,  900},
    {1920, 1200},
    {2560, 1600},
    // 16:9
    {1280,  720},
    {1366,  768},
    {1600,  900},
    {1920, 1080},
    {2560, 1440},
    {3840, 2160},
};
static const int NUM_RESOLUTIONS = 14;
static const int RES_VISIBLE_MAX = 6;  // 下拉最多同时显示几项

// ======================== 矩形着色器 ========================
static const char* rectVertSrc = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
out vec4 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* rectFragSrc = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)";

// ======================== 辅助：编译链接着色器 ========================
static unsigned int compileShader(GLenum type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
               fprintf(stderr,"Menu rect shader err: %s\n",log); }
    return s;
}

static unsigned int makeRectProgram() {
    unsigned int vs = compileShader(GL_VERTEX_SHADER, rectVertSrc);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, rectFragSrc);
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(prog, 512, nullptr, log);
                fprintf(stderr,"Menu rect link err: %s\n",log); }
    glDeleteShader(vs); glDeleteShader(fs);
    return prog;
}

// ======================== NDC → 像素坐标 ========================
float Menu::ndcXToPixel(float ndcX, int screenW) const {
    return (ndcX + 1.0f) * 0.5f * (float)screenW;
}

float Menu::ndcYToPixel(float ndcY, int screenH) const {
    // NDC y 向上，屏幕像素 y 向下
    return (1.0f - (ndcY + 1.0f) * 0.5f) * (float)screenH;
}

// ======================== Menu 实现 ========================
Menu::Menu() {}

void Menu::init(GLFWwindow* window, TextRenderer* textRenderer) {
    m_window = window;
    m_textRenderer = textRenderer;

    m_rectShader = makeRectProgram();
    glGenVertexArrays(1, &m_rectVAO);

    buildTitleButtons();
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void Menu::setState(MenuState s) {
    m_state = s;
    // 重置鼠标状态，避免从 gameplay 切回时残留的 pressed 状态导致需双击
    m_mousePressed    = false;
    m_mouseWasPressed = false;
    switch (s) {
        case MenuState::TITLE:
            buildTitleButtons();
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case MenuState::PAUSE:
            buildPauseButtons();
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case MenuState::SETTINGS:
            buildSettingsButtons();
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case MenuState::CONTROLS:
            buildControlsButtons();
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            break;
        case MenuState::GAMEPLAY:
            glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            g_firstMouse = true;  // 避免菜单期间停滞的 g_lastX/Y 导致视角跳动
            break;
        default: break;
    }
}

void Menu::setGlobals(bool* bloom, bool* flare, bool* autoExp,
                      float* bn, float* fl, float* manExp, bool* wireframe,
                      int* resIndex, bool* fullscr, int* scrW, int* scrH) {
    m_bloomEnabled   = bloom;
    m_flareEnabled   = flare;
    m_autoExposure   = autoExp;
    m_bloomStrength  = bn;
    m_flareIntensity = fl;
    m_manualExposure = manExp;
    m_wireframe      = wireframe;
    m_resolutionIndex = resIndex;
    m_fullscreen     = fullscr;
    m_screenWidth    = scrW;
    m_screenHeight   = scrH;
}

void Menu::togglePause() {
    if (m_state == MenuState::GAMEPLAY) {
        setState(MenuState::PAUSE);
    } else if (m_state == MenuState::PAUSE) {
        setState(MenuState::GAMEPLAY);
    }
}

void Menu::back() {
    if (m_state == MenuState::SETTINGS || m_state == MenuState::CONTROLS) {
        setState(m_prevState);
    }
}

void Menu::handleMousePos(double x, double y) {
    m_mouseX = (float)x;
    m_mouseY = (float)y;
}

void Menu::handleMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        m_mousePressed = (action == GLFW_PRESS);
    }
}

void Menu::handleScroll(double yoffset) {
    if (!m_resDropdownOpen || m_state != MenuState::SETTINGS) return;
    int maxScroll = NUM_RESOLUTIONS - RES_VISIBLE_MAX;
    if (maxScroll <= 0) return;
    m_resScrollOffset -= (int)yoffset;  // 滚轮向上 = 负数，偏移减小
    if (m_resScrollOffset < 0) m_resScrollOffset = 0;
    if (m_resScrollOffset > maxScroll) m_resScrollOffset = maxScroll;
}

// ======================== 按钮构建 ========================
void Menu::buildTitleButtons() {
    m_buttons.clear();
    // 4 个按钮，每个 h=0.10，间隙 0.04
    m_buttons.emplace_back(-0.3f,  0.22f, 0.60f, 0.10f, "Start",
        [this](){ setState(MenuState::GAMEPLAY); });
    m_buttons.emplace_back(-0.3f,  0.08f, 0.60f, 0.10f, "Settings",
        [this](){ m_prevState=m_state; setState(MenuState::SETTINGS); });
    m_buttons.emplace_back(-0.3f, -0.06f, 0.60f, 0.10f, "Controls",
        [this](){ m_prevState=m_state; setState(MenuState::CONTROLS); });
    m_buttons.emplace_back(-0.3f, -0.20f, 0.60f, 0.10f, "Exit",
        [this](){ m_quit = true; });
}

void Menu::buildPauseButtons() {
    m_buttons.clear();
    m_buttons.emplace_back(-0.3f,  0.20f, 0.60f, 0.12f, "Resume",
        [this](){ setState(MenuState::GAMEPLAY); });
    m_buttons.emplace_back(-0.3f, -0.05f, 0.60f, 0.12f, "Settings",
        [this](){ m_prevState=m_state; setState(MenuState::SETTINGS); });
    m_buttons.emplace_back(-0.3f, -0.30f, 0.60f, 0.12f, "Main Menu",
        [this](){ setState(MenuState::TITLE); });
}

void Menu::buildSettingsButtons() {
    m_buttons.clear();
    float bh = 0.09f;  // 按钮高度

    // 每行：标签（左）+ 值（右）
    auto rowY = [&](int r) -> float { return 0.40f - r * (bh + 0.03f); };

    // Row 0: Bloom
    m_buttons.emplace_back(-0.55f, rowY(0), 0.45f, bh, "Bloom",
        [this](){ if(m_bloomEnabled) *m_bloomEnabled = !*m_bloomEnabled; });
    m_buttons.emplace_back( 0.10f, rowY(0), 0.30f, bh, "val_bloom",
        [this](){ if(m_bloomEnabled) *m_bloomEnabled = !*m_bloomEnabled; });

    // Row 1: Lens Flare
    m_buttons.emplace_back(-0.55f, rowY(1), 0.45f, bh, "Lens Flare",
        [this](){ if(m_flareEnabled) *m_flareEnabled = !*m_flareEnabled; });
    m_buttons.emplace_back( 0.10f, rowY(1), 0.30f, bh, "val_flare",
        [this](){ if(m_flareEnabled) *m_flareEnabled = !*m_flareEnabled; });

    // Row 2: Auto Exposure
    m_buttons.emplace_back(-0.55f, rowY(2), 0.45f, bh, "Auto Exposure",
        [this](){ if(m_autoExposure) *m_autoExposure = !*m_autoExposure; });
    m_buttons.emplace_back( 0.10f, rowY(2), 0.30f, bh, "val_autoexp",
        [this](){ if(m_autoExposure) *m_autoExposure = !*m_autoExposure; });

    // Row 3: Wireframe
    m_buttons.emplace_back(-0.55f, rowY(3), 0.45f, bh, "Wireframe",
        [this](){ if(m_wireframe) *m_wireframe = !*m_wireframe; });
    m_buttons.emplace_back( 0.10f, rowY(3), 0.30f, bh, "val_wire",
        [this](){ if(m_wireframe) *m_wireframe = !*m_wireframe; });

    // Row 4: Fullscreen
    m_buttons.emplace_back(-0.55f, rowY(4), 0.45f, bh, "Fullscreen",
        [this](){ if(m_fullscreen) { *m_fullscreen = !*m_fullscreen; applyResolution(); } });
    m_buttons.emplace_back( 0.10f, rowY(4), 0.30f, bh, "val_fullscreen",
        [this](){ if(m_fullscreen) { *m_fullscreen = !*m_fullscreen; applyResolution(); } });

    // Row 5: Resolution（点击展开下拉菜单）
    m_buttons.emplace_back(-0.55f, rowY(5), 0.45f, bh, "Resolution",
        [this](){ /* 标签，无动作 */ });
    m_buttons.emplace_back( 0.10f, rowY(5), 0.30f, bh, "val_res",
        [this](){ m_resDropdownOpen = !m_resDropdownOpen; });

    // Back
    m_buttons.emplace_back(-0.3f, -0.82f, 0.60f, 0.10f, "Back",
        [this](){ back(); });
}

void Menu::buildControlsButtons() {
    m_buttons.clear();
    // 只有 Back 按钮，按键说明由 render() 绘制
    m_buttons.emplace_back(-0.3f, -0.82f, 0.60f, 0.10f, "Back",
        [this](){ back(); });
}

void Menu::applyResolution() {
    if (!m_resolutionIndex || !m_screenWidth || !m_screenHeight || !m_fullscreen || !m_window)
        return;
    int idx = *m_resolutionIndex;
    int w = RESOLUTIONS[idx][0];
    int h = RESOLUTIONS[idx][1];

    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    if (!mon) return;
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    if (!mode) return;

    if (*m_fullscreen) {
        glfwSetWindowMonitor(m_window, mon, 0, 0, w, h, mode->refreshRate);
    } else {
        int xpos = (mode->width  - w) / 2;
        int ypos = (mode->height - h) / 2;
        glfwSetWindowMonitor(m_window, nullptr, xpos, ypos, w, h, 0);
    }
    // glfwSetWindowMonitor 后同步获取 framebuffer size，不依赖回调
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    if (fbW > 0 && fbH > 0) {
        *m_screenWidth  = fbW;
        *m_screenHeight = fbH;
    }
}

// ======================== 按钮命中检测 ========================
bool Menu::inside(float mx, float my, const Button& b) const {
    return mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h;
}

int Menu::findHovered() const {
    float mxNDC = (m_mouseX / (float)800.0f) * 2.0f - 1.0f;  // 临时：用默认宽
    float myNDC = 1.0f - (m_mouseY / (float)600.0f) * 2.0f;
    // 正确版本在 render() 里做，这里留着兼容
    for (size_t i = 0; i < m_buttons.size(); i++) {
        if (inside(mxNDC, myNDC, m_buttons[i])) return (int)i;
    }
    return -1;
}

// ======================== 绘制原语 ========================
void Menu::drawRect(float x, float y, float w, float h,
                    float r, float g, float b, float a,
                    int screenW, int screenH) {
    // x,y,w,h 均为 NDC 坐标
    float verts[] = {
        x,   y,   r,g,b,a,
        x+w, y,   r,g,b,a,
        x+w, y+h, r,g,b,a,
        x,   y,   r,g,b,a,
        x+w, y+h, r,g,b,a,
        x,   y+h, r,g,b,a,
    };
    unsigned int vbo;
    glGenBuffers(1, &vbo);
    glBindVertexArray(m_rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDeleteBuffers(1, &vbo);
}

void Menu::drawMenuText(const std::string& text, float ndcX, float ndcY,
                         float pixelScale, const float color[3], int scrW, int scrH) {
    if (!m_textRenderer || text.empty()) return;

    float px = ndcXToPixel(ndcX, scrW);
    // ndcYToPixel returns from-top; ortho is bottom-up, flip
    float py = (float)scrH - ndcYToPixel(ndcY, scrH);
    glm::vec3 c(color[0], color[1], color[2]);
    //pixelScale: 以像素为单位的字符高度缩放因子
    m_textRenderer->renderText(text, px, py, pixelScale / 24.0f, c);
}

void Menu::drawButton(const Button& btn, bool hovered, int scrW, int scrH) {
    // 按钮背景
    float br = hovered ? 0.3f : 0.15f;
    float bg = hovered ? 0.5f : 0.25f;
    float bb = hovered ? 0.8f : 0.45f;

    glUseProgram(m_rectShader);
    drawRect(btn.x - 0.01f, btn.y - 0.01f, btn.w + 0.02f, btn.h + 0.02f,
             br * 0.5f, bg * 0.5f, bb * 0.5f, 0.6f, scrW, scrH);
    drawRect(btn.x, btn.y, btn.w, btn.h, br, bg, bb, 0.8f, scrW, scrH);

    // 按钮文字：用 FreeType 渲染
    std::string label = btn.text;
    // 设置页：根据 btn.text 显示对应值
    if (m_state == MenuState::SETTINGS) {
        if (btn.text == "val_bloom") {
            label = *m_bloomEnabled ? "ON" : "OFF";
        } else if (btn.text == "val_flare") {
            label = *m_flareEnabled ? "ON" : "OFF";
        } else if (btn.text == "val_autoexp") {
            label = *m_autoExposure ? "ON" : "OFF";
        } else if (btn.text == "val_wire") {
            label = *m_wireframe ? "ON" : "OFF";
        } else if (btn.text == "val_fullscreen") {
            label = *m_fullscreen ? "ON" : "OFF";
        } else if (btn.text == "val_res") {
            int idx = m_resolutionIndex ? *m_resolutionIndex : 0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%dx%d",
                     RESOLUTIONS[idx][0], RESOLUTIONS[idx][1]);
            label = buf;
        }
    }

    if (!label.empty() && m_textRenderer) {
        // ndcYToPixel returns Y from top; renderText ortho has Y=0 at bottom
        float btnTopPx    = ndcYToPixel(btn.y + btn.h, scrH);
        float btnBotPx    = ndcYToPixel(btn.y, scrH);
        float btnH        = btnBotPx - btnTopPx;
        float pixelScale  = btnH * 0.6f;
        float charW       = pixelScale * 0.55f;               // 等宽近似
        float textPixelW  = (float)label.length() * charW;
        float btnLeft     = ndcXToPixel(btn.x, scrW);
        float btnRight    = ndcXToPixel(btn.x + btn.w, scrW);
        float textPixelX  = btnLeft + (btnRight - btnLeft - textPixelW) * 0.5f;
        // 垂直居中：用 'M' 的实际字形度量算 baseline
        float textPixelY = 0.0f;
        float ascender = 0.0f, totalH = 0.0f;
        float scale = pixelScale / 24.0f;
        if (m_textRenderer->getGlyphMetrics(scale, ascender, totalH)) {
            // glyph center rel to baseline: (ascender - (totalH - ascender)) / 2
            // baseline = btnCenterY_up - glyphCenterRelToBaseline
            float btnCenterDown = btnTopPx + btnH * 0.5f;
            float btnCenterUp   = (float)scrH - btnCenterDown;
            textPixelY = btnCenterUp + (totalH * 0.5f - ascender);
        } else {
            // fallback：旧近似
            float capHeight   = pixelScale * 0.75f;
            float baselineFromTop = btnTopPx + (btnH + capHeight) * 0.5f;
            textPixelY  = (float)scrH - baselineFromTop;
        }

        m_textRenderer->renderText(label, textPixelX, textPixelY, pixelScale / 24.0f, glm::vec3(1.0f));
    }
}

// ======================== 主渲染 ========================
void Menu::render(int screenW, int screenH) {
    if (m_state == MenuState::GAMEPLAY) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 背景
    glUseProgram(m_rectShader);
    if (m_state == MenuState::PAUSE || m_state == MenuState::SETTINGS || m_state == MenuState::CONTROLS) {
        drawRect(-1.0f, -1.0f, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f, screenW, screenH);
    } else {
        drawRect(-1.0f, -1.0f, 2.0f, 2.0f, 0.0f, 0.02f, 0.06f, 1.0f, screenW, screenH);
    }

    // 标题文字（FreeType）
    const char* title = nullptr;
    float titleY = 0.6f;
    float titleScale = 48.0f;  // 像素高度
    switch (m_state) {
        case MenuState::TITLE:    title = "SOLAR SYSTEM"; titleY = 0.65f; titleScale = 64.0f; break;
        case MenuState::PAUSE:    title = "PAUSED";       titleY = 0.55f; break;
        case MenuState::SETTINGS: title = "SETTINGS";     titleY = 0.55f; break;
        case MenuState::CONTROLS: title = "CONTROLS";    titleY = 0.55f; break;
        default: break;
    }

    if (title && m_textRenderer) {
        // 标题居中：先估算像素宽度
        float charW = titleScale * 0.6f;
        float textPixelW = (float)strlen(title) * charW;
        float tx = ((float)screenW - textPixelW) * 0.5f;
        // "SOLAR SYSTEM" 向右偏移半个字符，抵消 SYSTEM 比 SOLAR 短的视觉不平衡
        if (m_state == MenuState::TITLE) tx += charW * 0.5f;
        // ndcYToPixel returns from-top; ortho is bottom-up, so flip
        float ty = (float)screenH - ndcYToPixel(titleY, screenH);
        m_textRenderer->renderText(title, tx, ty, titleScale / 24.0f, glm::vec3(1.0f, 0.9f, 0.3f));
    }

    // Controls 页面背景面板
    if (m_state == MenuState::CONTROLS) {
        // 圆角效果用两层模拟：外层暗边框 + 内层面板
        float px = -0.65f, py = -0.93f, pw = 1.30f, ph = 1.44f;
        drawRect(px, py, pw, ph, 0.08f, 0.08f, 0.12f, 0.65f, screenW, screenH);
        // 内部发光边线 (top)
        float edgeH = 2.0f / (float)screenH * 2.0f;  // 2px → NDC
        drawRect(px, py + ph - edgeH, pw, edgeH, 0.25f, 0.25f, 0.35f, 0.6f, screenW, screenH);
    }

    // 鼠标 NDC 坐标：用窗口逻辑像素尺寸转换（m_mouseX/Y 来自 glfwGetCursorPos，是逻辑像素）
    int winW = screenW, winH = screenH;
    if (m_window) {
        glfwGetWindowSize(m_window, &winW, &winH);
    }
    if (winW <= 0) winW = screenW;
    if (winH <= 0) winH = screenH;
    float mxNDC = (m_mouseX / (float)winW) * 2.0f - 1.0f;
    float myNDC = 1.0f - (m_mouseY / (float)winH) * 2.0f;

    int hovered = -1;
    for (size_t i = 0; i < m_buttons.size(); i++) {
        if (inside(mxNDC, myNDC, m_buttons[i])) { hovered = (int)i; break; }
    }

    // 分辨率下拉项悬停检测
    m_resHoverIdx = -1;
    if (m_resDropdownOpen && m_state == MenuState::SETTINGS) {
        int valResIdx = -1;
        for (size_t i = 0; i < m_buttons.size(); i++) {
            if (m_buttons[i].text == "val_res") { valResIdx = (int)i; break; }
        }
        if (valResIdx >= 0) {
            const Button& btn = m_buttons[valResIdx];
            float itemH = btn.h;
            float itemW = btn.w;
            float itemX = btn.x;
            float gap = 0.005f;
            int visibleMax = (NUM_RESOLUTIONS > RES_VISIBLE_MAX) ? RES_VISIBLE_MAX : NUM_RESOLUTIONS;
            // 先钳制滚动偏移
            int maxScr = NUM_RESOLUTIONS - RES_VISIBLE_MAX;
            if (maxScr < 0) maxScr = 0;
            // 悬停：只检测可见项 + 滚动条
            for (int vi = 0; vi < visibleMax; vi++) {
                int i = m_resScrollOffset + vi;
                if (i >= NUM_RESOLUTIONS) break;
                float itemY = btn.y - (vi + 1) * (itemH + gap);
                if (mxNDC >= itemX && mxNDC <= itemX + itemW &&
                    myNDC >= itemY && myNDC <= itemY + itemH) {
                    m_resHoverIdx = i;
                    break;
                }
            }
        }
    }

    if (m_mousePressed && !m_mouseWasPressed) {
        if (hovered >= 0) {
            m_buttons[hovered].action();
        } else if (m_resDropdownOpen && m_resHoverIdx >= 0) {
            // 点击了下拉项
            if (m_resolutionIndex) {
                *m_resolutionIndex = m_resHoverIdx;
                applyResolution();
            }
            m_resDropdownOpen = false;
            m_resScrollOffset = 0;
        } else if (m_resDropdownOpen && NUM_RESOLUTIONS > RES_VISIBLE_MAX) {
            // 检测是否点击了滚动条轨道
            int valResIdx2 = -1;
            for (size_t i = 0; i < m_buttons.size(); i++) {
                if (m_buttons[i].text == "val_res") { valResIdx2 = (int)i; break; }
            }
            if (valResIdx2 >= 0) {
                const Button& btn2 = m_buttons[valResIdx2];
                float itemH2 = btn2.h;
                float gap2   = 0.005f;
                int vc2 = RES_VISIBLE_MAX;
                float vh2 = (float)vc2 * (itemH2 + gap2) - gap2;
                float sbTX = btn2.x + btn2.w + 0.002f;
                float sbTY = btn2.y - vh2 - 0.002f;
                float sbTH = vh2 + 0.004f - 0.006f;
                float sbTW = 0.015f;
                if (mxNDC >= sbTX && mxNDC <= sbTX + sbTW &&
                    myNDC >= sbTY && myNDC <= sbTY + sbTH) {
                    // 点击了滚动条轨道：判断在拇指上方还是下方
                    int maxScr2 = NUM_RESOLUTIONS - RES_VISIBLE_MAX;
                    float thumbRatio = (float)vc2 / (float)NUM_RESOLUTIONS;
                    float thumbH = sbTH * thumbRatio;
                    float thumbTravel = sbTH - thumbH;
                    float thumbY = sbTY + thumbTravel * ((float)m_resScrollOffset / (float)maxScr2);
                    // 重新计算 thumbY（简化用）
                    thumbY = sbTY + thumbTravel * ((float)m_resScrollOffset / (float)maxScr2);
                    if (myNDC < thumbY) {
                        // 点在拇指上方，上滚一页
                        m_resScrollOffset -= RES_VISIBLE_MAX;
                    } else if (myNDC > thumbY + thumbH) {
                        // 点在拇指下方，下滚一页
                        m_resScrollOffset += RES_VISIBLE_MAX;
                    }
                    if (m_resScrollOffset < 0) m_resScrollOffset = 0;
                    if (m_resScrollOffset > maxScr2) m_resScrollOffset = maxScr2;
                } else {
                    m_resDropdownOpen = false;
                    m_resScrollOffset = 0;
                }
            } else {
                m_resDropdownOpen = false;
                m_resScrollOffset = 0;
            }
        } else if (m_resDropdownOpen) {
            // 点击了下拉以外的区域，收起下拉
            m_resDropdownOpen = false;
            m_resScrollOffset = 0;
        }
    }
    m_mouseWasPressed = m_mousePressed;

    // 绘制按钮
    for (size_t i = 0; i < m_buttons.size(); i++) {
        drawButton(m_buttons[i], (int)i == hovered, screenW, screenH);
    }

    // 分辨率下拉菜单
    if (m_resDropdownOpen && m_state == MenuState::SETTINGS) {
        int valResIdx = -1;
        for (size_t i = 0; i < m_buttons.size(); i++) {
            if (m_buttons[i].text == "val_res") { valResIdx = (int)i; break; }
        }
        if (valResIdx >= 0) {
            const Button& btn = m_buttons[valResIdx];
            float itemH = btn.h;
            float itemW = btn.w;
            float itemX = btn.x;
            float gap   = 0.005f;

            int maxScr     = NUM_RESOLUTIONS - RES_VISIBLE_MAX;
            if (maxScr < 0) maxScr = 0;
            if (m_resScrollOffset > maxScr) m_resScrollOffset = maxScr;
            if (m_resScrollOffset < 0) m_resScrollOffset = 0;

            int visibleCount = (NUM_RESOLUTIONS > RES_VISIBLE_MAX) ? RES_VISIBLE_MAX : NUM_RESOLUTIONS;
            float visibleH   = (float)visibleCount * (itemH + gap) - gap;

            // 下拉背景
            float sbW = 0.015f;  // 滚动条宽度 (NDC)
            float bgX = itemX - 0.005f;
            float bgY = btn.y - visibleH - 0.005f;
            float bgW = itemW + 0.01f + sbW;  // 留出滚动条空间
            float bgH = visibleH + 0.01f;
            drawRect(bgX, bgY, bgW, bgH, 0.06f, 0.06f, 0.10f, 0.97f, screenW, screenH);
            // 顶部亮边
            drawRect(bgX, bgY + bgH - 0.002f, bgW, 0.002f, 0.25f, 0.25f, 0.35f, 0.8f, screenW, screenH);

            // 滚动条
            if (NUM_RESOLUTIONS > RES_VISIBLE_MAX) {
                float sbTrackX = itemX + itemW + 0.002f;
                float sbTrackY = bgY + 0.003f;
                float sbTrackH = bgH - 0.006f;
                // 滚动条轨道（暗色背景）
                drawRect(sbTrackX, sbTrackY, sbW, sbTrackH,
                         0.10f, 0.10f, 0.14f, 0.8f, screenW, screenH);
                // 滚动条拇指
                float thumbRatio = (float)visibleCount / (float)NUM_RESOLUTIONS;
                float thumbH = sbTrackH * thumbRatio;
                float thumbTravel = sbTrackH - thumbH;
                float thumbY = sbTrackY + thumbTravel * ((float)m_resScrollOffset / (float)maxScr);
                drawRect(sbTrackX, thumbY, sbW, thumbH,
                         0.30f, 0.32f, 0.38f, 0.9f, screenW, screenH);

                // 滚动条点击检测（在点击处理部分做）
            }

            // 绘制可见选项
            for (int vi = 0; vi < visibleCount; vi++) {
                int i = m_resScrollOffset + vi;
                if (i >= NUM_RESOLUTIONS) break;
                float itemY = btn.y - (vi + 1) * (itemH + gap);
                bool isHovered = (m_resHoverIdx == i);
                bool isSelected = (m_resolutionIndex && *m_resolutionIndex == i);

                float r = isHovered ? 0.25f : (isSelected ? 0.12f : 0.04f);
                float g = isHovered ? 0.45f : (isSelected ? 0.20f : 0.04f);
                float b = isHovered ? 0.75f : (isSelected ? 0.35f : 0.08f);
                drawRect(itemX, itemY, itemW, itemH, r, g, b, 0.92f, screenW, screenH);

                char buf[32];
                snprintf(buf, sizeof(buf), "%dx%d", RESOLUTIONS[i][0], RESOLUTIONS[i][1]);
                float btnTopPx   = ndcYToPixel(itemY + itemH, screenH);
                float btnBotPx   = ndcYToPixel(itemY, screenH);
                float btnHpx     = btnBotPx - btnTopPx;
                float pixelScale = btnHpx * 0.55f;
                float charW      = pixelScale * 0.55f;
                float textPixelW = (float)strlen(buf) * charW;
                float btnLeft    = ndcXToPixel(itemX, screenW);
                float btnRight   = ndcXToPixel(itemX + itemW, screenW);
                float textPixelX = btnLeft + (btnRight - btnLeft - textPixelW) * 0.5f;
                float ascender2 = 0.0f, totalH2 = 0.0f;
                float scale2 = pixelScale / 24.0f;
                float textPixelY2 = 0.0f;
                if (m_textRenderer->getGlyphMetrics(scale2, ascender2, totalH2)) {
                    float itemCenterDown = btnTopPx + btnHpx * 0.5f;
                    float itemCenterUp   = (float)screenH - itemCenterDown;
                    textPixelY2 = itemCenterUp + (totalH2 * 0.5f - ascender2);
                } else {
                    float capHeight = pixelScale * 0.75f;
                    float baseline  = btnTopPx + (btnHpx + capHeight) * 0.5f;
                    textPixelY2 = (float)screenH - baseline;
                }
                float col        = isHovered ? 1.0f : (isSelected ? 0.9f : 0.8f);
                m_textRenderer->renderText(buf, textPixelX, textPixelY2,
                                          pixelScale / 24.0f, glm::vec3(col, col, col));
            }
        }
    }

    // 按键绑定说明（仅 Controls 页面）
    if (m_state == MenuState::CONTROLS) {
        struct KLine { const char* text; bool dim; };
        static const KLine lines[] = {
            {"--- Controls ---",          true},
            {"Camera:",                     true},
            {"  WASD     Move F/L/B/R",   false},
            {"  R/F      Up/Down",        false},
            {"  Q/E      Roll L/R",        false},
            {"  Mouse    Look, Scroll=Spd",false},
            {"Display:",                    true},
            {"  N        Direct output",    false},
            {"  T        Test sphere",      false},
            {"  G        G-Buffer debug",  false},
            {"  X        Diagnose",         false},
            {"  KP+/-    Exposure +/-0.5", false},
            {"System:",                    true},
            {"  ESC      Menu / Pause",    false},
        };
        const float dimCol[3] = {0.5f, 0.5f, 0.5f};
        const float valCol[3] = {0.85f, 0.85f, 0.85f};
        float kScale = 15.0f;
        float rowH   = kScale * 1.3f / (float)screenH * 2.0f;
        int numLines = (int)(sizeof(lines)/sizeof(lines[0]));
        float totalBlockNDC = (numLines - 1) * rowH;  // 文本块 NDC 总高度
        // Controls 页整体居中；Settings 页保留左对齐以免和按钮重叠
        bool centerText = (m_state == MenuState::CONTROLS);
        // 动态垂直居中：文本块放在标题下方到 Back 按钮上方之间
        float availTop    = 0.42f;   // 标题下方可用空间起点 (NDC)
        float availBot    = -0.72f;  // Back 按钮上方可用空间终点 (NDC)
        float centerNDC   = (availTop + availBot) * 0.5f;
        float startY      = centerNDC + totalBlockNDC * 0.5f;  // 第一行居中对齐到可用空间中心
        for (int i = 0; i < numLines; i++) {
            const float* c = lines[i].dim ? dimCol : valCol;
            float yNDC = startY - (float)i * rowH;
            float px;
            if (centerText) {
                // 每行各自居中（视觉更均匀）
                float lineW = (float)strlen(lines[i].text) * kScale * 0.6f;
                px = ((float)screenW - lineW) * 0.5f;
            } else {
                px = ndcXToPixel(-0.78f, screenW);
            }
            float py = (float)screenH - ndcYToPixel(yNDC, screenH);
            m_textRenderer->renderText(lines[i].text, px, py,
                                      kScale / 24.0f, glm::vec3(c[0],c[1],c[2]));
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}
