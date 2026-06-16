#pragma once
#include <glm/glm.hpp>
#include <string>

class Planet {
public:
    Planet(const std::string& name, float orbitRadius, float orbitPeriod, float rotationPeriod,
           float size, unsigned int textureID, const glm::vec3& color,
           float roughness = 0.7f, float metallic = 0.0f,
           bool hasAtmosphere = false);
    void update(float simulationTime);

    void draw(unsigned int shaderProgram, unsigned int sphereVAO, int indexCount,
              const glm::mat4& view, const glm::mat4& projection,
              const glm::vec3& viewPos, float lightIntensity, float sunRadius);

    void drawEmissive(unsigned int shader, unsigned int sphereVAO, int indexCount,
                      const glm::mat4& view, const glm::mat4& projection,
                      const glm::vec3& viewPos, float sunIntensity);

    void drawSimple(unsigned int shader, unsigned int sphereVAO, int indexCount,
                    const glm::mat4& view, const glm::mat4& projection,
                    const glm::vec3& color);

    glm::vec3 getPosition() const { return worldPosition; }
    float     getSize()     const { return size; }
    bool      isSun()          const { return isSunFlag; }
    bool      hasAtmosphere() const { return m_hasAtmosphere; }
    glm::vec3 getColor()    const { return color; }
    const std::string& getName() const { return name; }

private:
    std::string name;
    float orbitRadius;
    float orbitPeriod;
    float rotationPeriod;
    float size;
    unsigned int textureID;
    glm::vec3 color;
    float roughness;
    float metallic;
    bool  isSunFlag;
    bool  m_hasAtmosphere;
    glm::vec3 worldPosition;
    float orbitAngle;
    float rotationAngle;
};
