#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "planet.h"
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

Planet::Planet(const std::string& name, float orbitRadius, float orbitPeriod, float rotationPeriod,
               float size, unsigned int textureID, const glm::vec3& color,
               float roughness, float metallic, bool hasAtmosphere)
    : name(name), orbitRadius(orbitRadius), orbitPeriod(orbitPeriod),
      rotationPeriod(rotationPeriod), size(size), textureID(textureID),
      color(color), roughness(roughness), metallic(metallic),
      isSunFlag(orbitRadius < 0.001f), m_hasAtmosphere(hasAtmosphere),
      worldPosition(orbitRadius, 0.0f, 0.0f), orbitAngle(0.0f), rotationAngle(0.0f) {}

void Planet::update(float simulationTime) {
    const float secondsPerDay = 86400.0f;
    if (orbitPeriod > 0.0f) {
        float orbitSpeed = 2.0f * M_PI / (orbitPeriod * secondsPerDay);
        orbitAngle = fmod(simulationTime * orbitSpeed, 2.0f * M_PI);
    }
    if (rotationPeriod != 0.0f) {
        float rotSpeed = 2.0f * M_PI / (rotationPeriod * secondsPerDay);
        rotationAngle = fmod(simulationTime * rotSpeed, 2.0f * M_PI);
    }
    worldPosition = glm::vec3(orbitRadius * cos(orbitAngle), 0.0f, orbitRadius * sin(orbitAngle));
}

void Planet::draw(unsigned int shader, unsigned int sphereVAO, int indexCount,
                  const glm::mat4& view, const glm::mat4& projection,
                  const glm::vec3& viewPos, float lightIntensity, float sunRadius) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition);
    model = glm::scale(model, glm::vec3(size));

    glUseProgram(shader);

    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader, "viewPos"), 1, glm::value_ptr(viewPos));
    glUniform1f(glGetUniformLocation(shader, "roughness"), roughness);
    glUniform1f(glGetUniformLocation(shader, "metallic"), metallic);
    glUniform1f(glGetUniformLocation(shader, "lightIntensity"), lightIntensity);
    glUniform1f(glGetUniformLocation(shader, "sunRadius"), sunRadius);
    glUniform1i(glGetUniformLocation(shader, "hasAtmosphere"), m_hasAtmosphere ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(glGetUniformLocation(shader, "textureSampler"), 0);

    glBindVertexArray(sphereVAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Planet::drawEmissive(unsigned int shader, unsigned int sphereVAO, int indexCount,
                          const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& viewPos, float sunIntensity) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPosition);
    model = glm::scale(model, glm::vec3(size));

    glUseProgram(shader);

    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(shader, "uSunColor"), 1, glm::value_ptr(color));
    glUniform1f(glGetUniformLocation(shader, "uSunIntensity"), sunIntensity);
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
