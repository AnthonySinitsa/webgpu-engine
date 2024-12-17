#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    void setOrthographicProjection(float left, float right, float top, float bottom, float near, float far);
    void setPerspectiveProjection(float fovy, float aspect, float near, float far);

    void setViewTarget(glm::vec3 position, glm::vec3 target, glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
    void setViewYXZ(glm::vec3 position, glm::vec3 rotation);

    const glm::mat4& getProjection() const { return projectionMatrix; }
    const glm::mat4& getView() const { return viewMatrix; }
    const glm::mat4& getInverseView() const { return inverseViewMatrix; }
    const glm::vec3& getPosition() const { return position; }
    const glm::vec3& getRotation() const { return rotation; }

    void setPosition(const glm::vec3& pos) { position = pos; updateView(); }
    void setRotation(const glm::vec3& rot) { rotation = rot; updateView(); }

private:
    void updateView();

    glm::mat4 projectionMatrix{1.f};
    glm::mat4 viewMatrix{1.f};
    glm::mat4 inverseViewMatrix{1.f};
    
    glm::vec3 position{};
    glm::vec3 rotation{};
};