#include "camera.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

Camera::Camera(glm::dvec3 pos, float yaw, float pitch, float roll, float fov, float speed, float sensitivity)
    : position(pos), worldUp(0.0f, 1.0f, 0.0f),
      fov(fov), movementSpeed(speed), mouseSensitivity(sensitivity), currentSpeed(speed) {
    // 用旧系统的 front 公式构造初始方向，保证与 config.ini 兼容
    glm::dvec3 f_d;
    f_d.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f_d.y = sin(glm::radians(pitch));
    f_d.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(glm::vec3(f_d));

    // 直接从 front 构建正交基
    right = glm::normalize(glm::cross(front, worldUp));
    up    = glm::cross(right, front);

    // 用正交基构建四元数（列主序：col0=right, col1=up, col2=-front）
    glm::mat3 rotMat(right, up, -front);
    orientation = glm::quat_cast(rotMat);

    // 应用初始滚转角
    if (roll != 0.0f) {
        glm::quat qRoll = glm::angleAxis(glm::radians(roll), front);
        orientation = glm::normalize(qRoll * orientation);
    }
}

void Camera::updateVectors() {
    // 从四元数直接导出三轴，保留完整滚转
    front = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);
    up    = orientation * glm::vec3(0.0f, 1.0f, 0.0f);
}

void Camera::processKeyboard(int key, int action, float) {
    if (action == GLFW_PRESS || action == GLFW_RELEASE) {
        bool pressed = (action == GLFW_PRESS);
        switch (key) {
            case GLFW_KEY_W: keyW = pressed; break;
            case GLFW_KEY_A: keyA = pressed; break;
            case GLFW_KEY_S: keyS = pressed; break;
            case GLFW_KEY_D: keyD = pressed; break;
            case GLFW_KEY_Q: keyQ = pressed; break;
            case GLFW_KEY_E: keyE = pressed; break;
            case GLFW_KEY_R: keyR = pressed; break;
            case GLFW_KEY_F: keyF = pressed; break;
        }
    }
}

void Camera::update(float deltaTime) {
    double dvelocity = (double)currentSpeed * (double)deltaTime;
    // 将单精度方向向量转为双精度位移
    glm::dvec3 f_d(front.x, front.y, front.z);
    glm::dvec3 r_d(right.x, right.y, right.z);
    glm::dvec3 u_d(up.x,    up.y,    up.z);
    if (keyW) position += f_d * dvelocity;
    if (keyS) position -= f_d * dvelocity;
    if (keyA) position -= r_d * dvelocity;
    if (keyD) position += r_d * dvelocity;
    if (keyR) position += u_d * dvelocity;
    if (keyF) position -= u_d * dvelocity;

    // Q/E 滚转：绕视线方向 (local Z)
    const float rollSpeed = 50.0f;
    if (keyQ || keyE) {
        float angle = glm::radians(rollSpeed * deltaTime);
        if (keyQ) angle = -angle;   // Q = 顺时针
        // E = 逆时针（默认正角度）
        glm::quat qRoll = glm::angleAxis(angle, front);
        orientation = glm::normalize(qRoll * orientation);
        updateVectors();
    }
}

void Camera::processMouseMovement(float xoffset, float yoffset) {
    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    // 俯仰：绕相机自身 right 轴旋转（正角度 = 抬头）
    glm::quat qPitch = glm::angleAxis(glm::radians(yoffset), right);
    // 偏航：绕相机自身 up 轴旋转（负角度 = 右转）
    glm::quat qYaw   = glm::angleAxis(glm::radians(-xoffset), up);

    orientation = glm::normalize(qYaw * qPitch * orientation);
    updateVectors();
}

void Camera::processMouseScroll(float yoffset) {
    // 乘法缩放：每格滚轮缩放 1.5x 或 1/1.5x，无上限但不可为0
    float factor = 1.0f + yoffset * 0.5f;
    currentSpeed *= factor;
    // 合理的速度范围：1e-6 ~ 1e12（覆盖从微米/秒到天文单位/秒）
    if (currentSpeed < 1e-6f) currentSpeed = 1e-6f;
    if (currentSpeed > 1e12f) currentSpeed = 1e12f;
}

glm::mat4 Camera::getViewMatrix() const {
    // 将双精度位置转为单精度传递给OpenGL（视图矩阵本身用单精度计算）
    glm::vec3 posF = glm::vec3(position);
    return glm::lookAt(posF, posF + front, up);
}
