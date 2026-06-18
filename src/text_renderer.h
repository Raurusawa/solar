#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <map>
#include <string>

struct Character {
    GLuint textureID;
    glm::ivec2 size;       // glyph bitmap size
    glm::ivec2 bearing;    // offset from baseline to top-left of glyph
    GLuint   advance;      // horizontal advance to next glyph
};

class TextRenderer {
public:
    TextRenderer(int screenWidth, int screenHeight);
    ~TextRenderer();

    void renderText(const std::string& text, float x, float y,
                    float scale, glm::vec3 color);
    void resize(int w, int h) { m_screenWidth = w; m_screenHeight = h; }

    // 计算文本渲染后的总像素宽度（用于水平居中）
    float getTextWidth(const std::string& text, float scale) const;

    // 获取典型大写字母（'M'）的字形度量，用于垂直居中计算
    // ascenderOut: baseline 到字形顶部的距离 (像素, 已乘 scale)
    // totalHeightOut: 字形总高度 (像素, 已乘 scale)
    // 返回 false  if 'M' 未加载
    bool getGlyphMetrics(float scale, float& ascenderOut, float& totalHeightOut) const;

private:
    void loadFont(const std::string& fontPath, int fontSize);

    std::map<GLchar, Character> m_characters;
    GLuint m_vao, m_vbo;
    GLuint m_shader;
    int m_screenWidth, m_screenHeight;
};
