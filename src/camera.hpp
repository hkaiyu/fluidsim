#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class ArcballCamera
{
public:
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjMatrix() const;

    glm::vec3 getViewDir() const;
    glm::vec3 getRightVec() const;
    glm::vec3 getCameraPos() const;

    // Initialization
    void setCameraView(glm::vec3 cameraPos, glm::vec3 lookAt, glm::vec3 up);
    void setPerspectiveProjection(float fov, float aspect, float near, float far);

    // Update
    void updateFov(float fov);
    void updateAspect(float aspect);
    void updateNearPlane(float near);
    void updateFarPlane(float far);

    // GLFW events
    void processGLFWScrollEvent(float xoffset, float yoffset);
    void processGLFWMouseButtonEvent(int button, int action, int mods, float xpos, float ypos);
    void processGLFWCursorMoveEvent(float xpos, float ypos, float vpwidth, float vpheight);

private:
    void updateView();
    void updatePerspective();
private:
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 cameraPos;
    glm::vec3 lookAt;
    glm::vec3 up;
    float lastMouseX, lastMouseY;
    float fov, aspect;
    float _near, _far;
    float scrollSensitivity{ 3.0f };
    bool mouseIsDragging = false;
};
