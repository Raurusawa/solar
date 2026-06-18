#pragma once
#include <string>
#include <vector>
#include <functional>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// 前向声明，避免 circular include
class TextRenderer;

enum class MenuState {
    TITLE,
    PAUSE,
    SETTINGS,
    CONTROLS,
    GAMEPLAY
};

class Menu {
public:
    Menu();
    void init(GLFWwindow* window, TextRenderer* textRenderer);
    void setState(MenuState s);
    MenuState getState() const { return m_state; }
    bool isGameplay() const { return m_state == MenuState::GAMEPLAY; }
    void togglePause();
    void back();
    bool isQuit() const { return m_quit; }

    void handleMousePos(double x, double y);
    void handleMouseButton(int button, int action);
    void handleScroll(double yoffset);
    void render(int screenW, int screenH);

    void setGlobals(bool* bloom, bool* flare, bool* autoExp,
                    float* bn, float* fl,
                    float* manExp, bool* wireframe,
                    int* resIndex, bool* fullscr, int* scrW, int* scrH);

    void applyResolution();

private:
    struct Button {
        float x, y, w, h;       // NDC [-1,1]
        std::string text;
        std::function<void()> action;
        Button() {}
        Button(float x_, float y_, float w_, float h_, std::string t, std::function<void()> a)
            : x(x_), y(y_), w(w_), h(h_), text(std::move(t)), action(std::move(a)) {}
    };

    MenuState m_state = MenuState::TITLE;
    MenuState m_prevState = MenuState::TITLE;
    GLFWwindow* m_window = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    std::vector<Button> m_buttons;
    unsigned int m_rectShader = 0;   // 按钮背景矩形着色器
    unsigned int m_rectVAO = 0;

    float m_mouseX = 0, m_mouseY = 0;
    bool m_mousePressed = false;
    bool m_mouseWasPressed = false;
    bool m_quit = false;

    // 分辨率下拉菜单状态
    bool m_resDropdownOpen = false;
    int  m_resHoverIdx = -1;
    int  m_resScrollOffset = 0;  // 滚动偏移（首个可见项索引）

    // 全局设置引用
    bool* m_bloomEnabled = nullptr;
    bool* m_flareEnabled = nullptr;
    bool* m_autoExposure = nullptr;
    float* m_bloomStrength = nullptr;
    float* m_flareIntensity = nullptr;
    float* m_manualExposure = nullptr;
    bool* m_wireframe = nullptr;
    int*  m_resolutionIndex = nullptr;
    bool* m_fullscreen = nullptr;
    int*  m_screenWidth = nullptr;
    int*  m_screenHeight = nullptr;

    // 文字渲染辅助：NDC 坐标 → 屏幕像素坐标
    float ndcXToPixel(float ndcX, int screenW) const;
    float ndcYToPixel(float ndcY, int screenH) const;

    void buildTitleButtons();
    void buildPauseButtons();
    void buildSettingsButtons();
    void buildControlsButtons();

    void drawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a,
                  int screenW, int screenH);
    void drawButton(const Button& btn, bool hovered, int scrW, int scrH);
    void drawMenuText(const std::string& text, float ndcX, float ndcY,
                      float pixelScale, const float color[3], int scrW, int scrH);

    bool inside(float mx, float my, const Button& b) const;
    int  findHovered() const;
};
