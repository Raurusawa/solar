#include "text_renderer.h"
#include <iostream>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glm/gtc/matrix_transform.hpp>

static const char* g_vertSrc = R"(
#version 330 core
layout (location = 0) in vec4 vertex;
out vec2 TexCoords;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
    TexCoords = vertex.zw;
}
)";

static const char* g_fragSrc = R"(
#version 330 core
in vec2 TexCoords;
out vec4 color;
uniform sampler2D text;
uniform vec3 textColor;
void main() {
    vec4 sampled = vec4(1.0, 1.0, 1.0, texture(text, TexCoords).r);
    color = vec4(textColor, 1.0) * sampled;
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "TextRenderer shader compile error: " << log << std::endl;
    }
    return s;
}

TextRenderer::TextRenderer(int screenWidth, int screenHeight)
    : m_vao(0), m_vbo(0), m_shader(0),
      m_screenWidth(screenWidth), m_screenHeight(screenHeight)
{
    // Compile shader program (inline)
    GLuint vs = compileShader(GL_VERTEX_SHADER, g_vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, g_fragSrc);
    m_shader = glCreateProgram();
    glAttachShader(m_shader, vs);
    glAttachShader(m_shader, fs);
    glLinkProgram(m_shader);
    GLint ok;
    glGetProgramiv(m_shader, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_shader, 512, nullptr, log);
        std::cerr << "TextRenderer link error: " << log << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Try to load a built-in Windows font
    const char* fontPaths[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        nullptr
    };
    for (int i = 0; fontPaths[i]; ++i) {
        loadFont(fontPaths[i], 24);
        if (!m_characters.empty()) break;
    }
}

TextRenderer::~TextRenderer() {
    for (auto& kv : m_characters)
        glDeleteTextures(1, &kv.second.textureID);
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteProgram(m_shader);
}

void TextRenderer::loadFont(const std::string& fontPath, int fontSize) {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cerr << "TextRenderer: FT_Init_FreeType failed" << std::endl;
        return;
    }
    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        std::cerr << "TextRenderer: failed to load font " << fontPath << std::endl;
        FT_Done_FreeType(ft);
        return;
    }
    FT_Set_Pixel_Sizes(face, 0, fontSize);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (GLubyte c = 32; c < 127; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            std::cerr << "TextRenderer: failed to load glyph " << (int)c << std::endl;
            continue;
        }
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                     face->glyph->bitmap.width,
                     face->glyph->bitmap.rows,
                     0, GL_RED, GL_UNSIGNED_BYTE,
                     face->glyph->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character ch = {
            tex,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            (GLuint)face->glyph->advance.x
        };
        m_characters[c] = ch;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    std::cout << "TextRenderer: loaded " << m_characters.size()
              << " glyphs from " << fontPath << std::endl;
}

void TextRenderer::renderText(const std::string& text,
                               float x, float y,
                               float scale, glm::vec3 color)
{
    if (m_characters.empty()) return;

    glm::mat4 proj = glm::ortho(0.0f, (float)m_screenWidth,
                                0.0f, (float)m_screenHeight);
    glUseProgram(m_shader);
    glUniformMatrix4fv(glGetUniformLocation(m_shader, "projection"),
                        1, GL_FALSE, &proj[0][0]);
    glUniform3f(glGetUniformLocation(m_shader, "textColor"),
                color.r, color.g, color.b);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_vao);

    for (auto ch : text) {
        auto it = m_characters.find(ch);
        if (it == m_characters.end()) continue;
        Character& c = it->second;

        float xpos = x + c.bearing.x * scale;
        float ypos = y - (c.size.y - c.bearing.y) * scale;
        float w = c.size.x * scale;
        float h = c.size.y * scale;

        float verts[6][4] = {
            { xpos,     ypos + h, 0.0f, 0.0f },
            { xpos,     ypos,     0.0f, 1.0f },
            { xpos + w, ypos,     1.0f, 1.0f },

            { xpos,     ypos + h, 0.0f, 0.0f },
            { xpos + w, ypos,     1.0f, 1.0f },
            { xpos + w, ypos + h, 1.0f, 0.0f },
        };

        glBindTexture(GL_TEXTURE_2D, c.textureID);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        x += (c.advance >> 6) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

bool TextRenderer::getGlyphMetrics(float scale,
                                  float& ascenderOut,
                                  float& totalHeightOut) const {
    auto it = m_characters.find('M');  // 'M' 代表典型大写字母
    if (it == m_characters.end()) return false;
    const Character& c = it->second;
    ascenderOut  = (float)c.bearing.y * scale;
    totalHeightOut = (float)c.size.y      * scale;
    return true;
}
