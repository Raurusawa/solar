#pragma once
#include <string>

unsigned int createShaderProgram(const char* vertexSource, const char* fragmentSource);
unsigned int createShaderProgramFromFiles(const char* vertexPath, const char* fragmentPath);
std::string readShaderFile(const char* path);
