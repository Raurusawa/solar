#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "planet.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

// 静态成员定义
double Planet::s_epochDays = 0.0;

Planet::Planet(const std::string& name, float orbitRadius, float orbitPeriod, float rotationPeriod,
               float size, unsigned int textureID, const glm::vec3& color,
               float roughness, float metallic, bool hasAtmosphere)
    : name(name), orbitRadius(orbitRadius), orbitPeriod(orbitPeriod),
      rotationPeriod(rotationPeriod), size(size), textureID(textureID),
      color(color), roughness(roughness), metallic(metallic),
      isSunFlag(orbitRadius < 0.001f), m_hasAtmosphere(hasAtmosphere),
      worldPosition(orbitRadius, 0.0f, 0.0f), orbitAngle(0.0f), rotationAngle(0.0f) {}

void Planet::setOrbitalElements(float inclination, float eccentricity,
                                float longitudeAscendingNode, float argumentOfPeriapsis,
                                float meanAnomalyAtEpoch, float axialTilt) {
    m_inclination           = inclination;
    m_eccentricity          = eccentricity;
    m_longitudeAscendingNode = longitudeAscendingNode;
    m_argumentOfPeriapsis   = argumentOfPeriapsis;
    m_meanAnomalyAtEpoch    = meanAnomalyAtEpoch;
    m_axialTilt             = axialTilt;
}

void Planet::setEpochDays(double daysSinceJ2000) {
    s_epochDays = daysSinceJ2000;
}

void Planet::update(double simulationTime) {
    const double secondsPerDay = 86400.0;
    const float deg2rad = M_PI / 180.0f;

    if (orbitPeriod > 0.0f) {
        // 1. 计算自 J2000.0 起的总天数
        double daysSinceEpoch = s_epochDays + simulationTime / secondsPerDay;

        // 2. 平近点角 M = M0 + n * days
        double n = 360.0 / orbitPeriod;  // 每日角速度 (度/日)
        double M = fmod(m_meanAnomalyAtEpoch + n * daysSinceEpoch, 360.0);
        if (M < 0.0) M += 360.0;
        double M_rad = M * deg2rad;

        // 3. 求解开普勒方程: M = E - e * sin(E)  (牛顿迭代)
        double E = M_rad;
        double e = m_eccentricity;
        if (e > 0.0) {
            for (int i = 0; i < 10; i++) {
                double sinE = sin(E);
                double cosE = cos(E);
                double dE = (E - e * sinE - M_rad) / (1.0 - e * cosE);
                E -= dE;
                if (fabs(dE) < 1e-12) break;
            }
        }
        orbitAngle = (float)E;  // 存储偏近点角

        // 4. 真近点角: nu = 2 * atan2(sqrt(1+e)*sin(E/2), sqrt(1-e)*cos(E/2))
        double cosE = cos(E);
        double sqrt_1me = sqrt(1.0 - e);
        double sqrt_1pe = sqrt(1.0 + e);
        double nu = 2.0 * atan2(sqrt_1pe * sin(E * 0.5), sqrt_1me * cos(E * 0.5));

        // 5. 日心距离: r = a * (1 - e * cos(E))
        float r = orbitRadius * (float)(1.0 - e * cosE);

        // 6. 天体力学标准旋转: 近日点幅角 ω → 倾角 i → 升交点黄经 Ω
        //    映射: 标准天文坐标 (XY=黄道面, Z=北) → 引擎坐标 (XZ=黄道面, Y=上)
        float w = m_argumentOfPeriapsis * deg2rad;
        float i = m_inclination * deg2rad;
        float O = m_longitudeAscendingNode * deg2rad;
        float cosI = cos(i), sinI = sin(i);
        float cosO = cos(O), sinO = sin(O);

        // 参数纬度 u = nu + ω
        float u = nu + w;
        float cosU = cos(u), sinU = sin(u);

        // 标准天文坐标 (ecliptic: XY=面, Z=北)
        float x_ecl = r * (cosU * cosO - sinU * sinO * cosI);
        float y_ecl = r * (cosU * sinO + sinU * cosO * cosI);
        float z_ecl = r * sinU * sinI;

        // 映射到引擎坐标: 天球 Z(北) → 引擎 Y(上),  天球 Y → 引擎 Z
        worldPosition = glm::vec3(x_ecl, z_ecl, y_ecl);
    }

    // 自转
    if (rotationPeriod != 0.0f) {
        double rotSpeed = 2.0 * M_PI / (fabs(rotationPeriod) * secondsPerDay);
        rotationAngle = fmod(simulationTime * rotSpeed, 2.0 * M_PI);
        if (rotationPeriod < 0.0f)
            rotationAngle = -rotationAngle;  // 逆行自转
    }

    // 更新卫星位置：绕母行星轨道，位于行星赤道面内
    // 使用与主行星一致的 J2000.0 历元，计算真实相位
    for (auto& m : moons) {
        double daysSinceEpoch = s_epochDays + simulationTime / secondsPerDay;
        double n_moon = 360.0 / m.orbitPeriod;  // 每日角速度 (度/日)
        double M_moon = fmod(m.meanAnomalyAtEpoch + n_moon * daysSinceEpoch, 360.0);
        if (M_moon < 0.0) M_moon += 360.0;
        float moonAngle = (float)(M_moon * deg2rad);
        // XZ 面正圆，然后绕 X 轴旋转 axialTilt 使轨道面与行星赤道面对齐
        float cosTilt = cos(m_axialTilt * deg2rad);
        float sinTilt = sin(m_axialTilt * deg2rad);
        glm::vec3 localPos(
            m.orbitRadius * cos(moonAngle),
            -m.orbitRadius * sin(moonAngle) * sinTilt,
            m.orbitRadius * sin(moonAngle) * cosTilt
        );
        m.worldPosition = worldPosition + localPos;
    }
}

void Planet::addMoon(const std::string& name, float orbitRadius, float orbitPeriod,
                     float meanAnomalyAtEpoch, float size, const glm::vec3& color,
                     unsigned int textureID) {
    Moon m;
    m.name = name;
    m.orbitRadius = orbitRadius;
    m.orbitPeriod = orbitPeriod;
    m.meanAnomalyAtEpoch = meanAnomalyAtEpoch;
    m.size = size;
    m.color = color;
    m.textureID = textureID;
    m.worldPosition = worldPosition + glm::vec3(orbitRadius, 0.0f, 0.0f);
    moons.push_back(m);
}

void Planet::draw(unsigned int shader, unsigned int sphereVAO, int indexCount,
                  const glm::mat4& view, const glm::mat4& viewRot, const glm::mat4& projection,
                  const glm::vec3& viewPos, float lightIntensity, float sunRadius) {
    // 相机相对坐标：用 worldPosition - viewPos 避免 float 精度丢失
    // 当行星世界坐标很大但半径很小时（如卫星），原始 worldPos 的 float
    // 精度不足以表示 sub-radius 级别的顶点位移
    // viewPos 在 view 矩阵中已经体现，所以这里用相对坐标构造 model
    // vertex shader 里 FragPos = model * pos + cameraPos 恢复世界坐标
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition - viewPos);
    // 静态轴向倾斜: 极轴从 Y 向 Z 偏转 axialTilt 度 (绕世界 X 轴)
    if (!isSunFlag && m_axialTilt != 0.0f) {
        model = glm::rotate(model, glm::radians(m_axialTilt), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    // 每日自转: 绕局部 Y 轴 (已倾斜的极轴)
    if (!isSunFlag) {
        model = glm::rotate(model, rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    model = glm::scale(model, glm::vec3(size));

    glUseProgram(shader);

    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "viewRot"), 1, GL_FALSE, glm::value_ptr(viewRot));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader, "cameraPos"), 1, glm::value_ptr(viewPos));
    glUniform3fv(glGetUniformLocation(shader, "viewPos"), 1, glm::value_ptr(viewPos));
    glUniform1f(glGetUniformLocation(shader, "roughness"), roughness);
    glUniform1f(glGetUniformLocation(shader, "metallic"), metallic);
    glUniform1f(glGetUniformLocation(shader, "lightIntensity"), lightIntensity);
    glUniform1f(glGetUniformLocation(shader, "sunRadius"), sunRadius);
    glUniform1i(glGetUniformLocation(shader, "hasAtmosphere"), m_hasAtmosphere ? 1 : 0);
    glUniform3f(glGetUniformLocation(shader, "uBaseColor"), 1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(shader, "uShadowFactor"), 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(glGetUniformLocation(shader, "textureSampler"), 0);

    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Planet::drawEmissive(unsigned int shader, unsigned int sphereVAO, int indexCount,
                          const glm::mat4& view, const glm::mat4& viewRot, const glm::mat4& projection,
                          const glm::vec3& viewPos, float sunIntensity) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition - viewPos);
    // 太阳自转: 静态轴向倾斜 + 绕局部Y自转
    if (m_axialTilt != 0.0f) {
        model = glm::rotate(model, glm::radians(m_axialTilt), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model = glm::rotate(model, rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(size));

    glUseProgram(shader);

    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "viewRot"), 1, GL_FALSE, glm::value_ptr(viewRot));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader, "cameraPos"), 1, glm::value_ptr(viewPos));
    glUniform3fv(glGetUniformLocation(shader, "uSunColor"), 1, glm::value_ptr(color));
    glUniform1f(glGetUniformLocation(shader, "uSunIntensity"), sunIntensity);
    // planet.vert 第21行: FragPos = relPos + cameraPos，已还原世界坐标
    // 因此 viewPos 必须是世界空间相机坐标，不能是 (0,0,0)
    glUniform3fv(glGetUniformLocation(shader, "viewPos"), 1, glm::value_ptr(viewPos));

    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Planet::drawSimple(unsigned int shader, unsigned int sphereVAO, int indexCount,
                        const glm::mat4& view, const glm::mat4& projection,
                        const glm::vec3& color) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition);
    model = glm::scale(model, glm::vec3(size));

    glUseProgram(shader);
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader, "color"), 1, glm::value_ptr(color));

    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}
