#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Moon {
    std::string name;
    float orbitRadius;          // 绕母行星轨道半径 (system units)
    float orbitPeriod;          // 地球日
    float meanAnomalyAtEpoch;   // J2000.0 平近点角 (度)
    float size;
    glm::vec3 color;            // fallback 颜色
    unsigned int textureID = 0; // 纹理（0 = 使用 fallback 颜色）
    glm::vec3 worldPosition;    // 每帧更新
};

class Planet {
public:
    Planet(const std::string& name, float orbitRadius, float orbitPeriod, float rotationPeriod,
           float size, unsigned int textureID, const glm::vec3& color,
           float roughness = 0.7f, float metallic = 0.0f,
           bool hasAtmosphere = false);

    // 设置轨道力学参数 (J2000.0 黄道面)
    void setOrbitalElements(float inclination, float eccentricity,
                            float longitudeAscendingNode, float argumentOfPeriapsis,
                            float meanAnomalyAtEpoch, float axialTilt);

    // 设置历元偏移 (J2000.0 到模拟起点的天数)
    static void setEpochDays(double daysSinceJ2000);

    void update(double simulationTime);

    void draw(unsigned int shaderProgram, unsigned int sphereVAO, int indexCount,
              const glm::mat4& view, const glm::mat4& viewRot, const glm::mat4& projection,
              const glm::vec3& viewPos, float lightIntensity, float sunRadius);

    void drawEmissive(unsigned int shader, unsigned int sphereVAO, int indexCount,
                      const glm::mat4& view, const glm::mat4& viewRot, const glm::mat4& projection,
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

    // 卫星
    void addMoon(const std::string& name, float orbitRadius, float orbitPeriod,
                 float meanAnomalyAtEpoch, float size, const glm::vec3& color,
                 unsigned int textureID = 0);
    const std::vector<Moon>& getMoons() const { return moons; }

private:
    std::string name;
    float orbitRadius;       // 半长轴
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

    // 轨道力学参数
    float m_inclination = 0.0f;
    float m_eccentricity = 0.0f;
    float m_longitudeAscendingNode = 0.0f;
    float m_argumentOfPeriapsis = 0.0f;
    float m_meanAnomalyAtEpoch = 0.0f;
    float m_axialTilt = 0.0f;

    // 静态历元偏移: J2000.0 → 模拟起点 的天数
    static double s_epochDays;

    std::vector<Moon> moons;
};
