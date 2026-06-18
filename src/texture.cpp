#include "texture.h"
#include <GL/glew.h>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

unsigned int loadTexture(const std::string& path, const glm::vec3& fallbackColor) {
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    int width, height, nrChannels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 0);
    if (data) {
        // 水平镜像翻转：左右交换每行像素
        int rowSize = width * nrChannels;
        for (int y = 0; y < height; y++) {
            unsigned char* row = data + y * rowSize;
            for (int x = 0; x < width / 2; x++) {
                int left  = x * nrChannels;
                int right = (width - 1 - x) * nrChannels;
                for (int c = 0; c < nrChannels; c++) {
                    unsigned char tmp = row[left + c];
                    row[left + c] = row[right + c];
                    row[right + c] = tmp;
                }
            }
        }
        GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(data);
        std::cout << "Loaded texture: " << path << std::endl;
    } else {
        std::cout << "Failed to load texture: " << path << ". Using fallback color." << std::endl;
        // 1x1 纯色纹理
        unsigned char color[3] = {
            (unsigned char)(fallbackColor.r * 255),
            (unsigned char)(fallbackColor.g * 255),
            (unsigned char)(fallbackColor.b * 255)
        };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, color);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return textureID;
}
