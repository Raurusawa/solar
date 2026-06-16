#include "shader.h"
#include <GL/glew.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

std::string readShaderFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static unsigned int compileShader(unsigned int type, const char* source) {
    unsigned int id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    int success;
    glGetShaderiv(id, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(id, 512, nullptr, info);
        std::cerr << "Shader compile error (" << type << "): " << info << std::endl;
    }
    return id;
}

unsigned int createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    unsigned int vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(program, 512, nullptr, info);
        std::cerr << "Program link error: " << info << std::endl;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

unsigned int createShaderProgramFromFiles(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode = readShaderFile(vertexPath);
    std::string fragmentCode = readShaderFile(fragmentPath);
    if (vertexCode.empty() || fragmentCode.empty()) return 0;
    return createShaderProgram(vertexCode.c_str(), fragmentCode.c_str());
}
