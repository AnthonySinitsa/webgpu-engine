#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Star {
    float position[3];  // x, y, z position
};

struct CameraUniforms {
    glm::mat4 view;
    glm::mat4 proj;
};

class GalaxyWebSystem {
public:
    GalaxyWebSystem(WGPUDevice device);
    ~GalaxyWebSystem();

    void render(WGPURenderPassEncoder renderPass);
    void cleanup();
    void updateCamera(float deltaTime);

private:
    static constexpr int NUM_STARS = 10;
    
    void createBuffers();
    void createPipeline();
    void initStars();
    void updateUniforms();

    WGPUDevice device;
    WGPUBuffer vertexBuffer;
    WGPUBuffer uniformBuffer;
    WGPURenderPipeline pipeline;
    WGPUBindGroup bindGroup;
    WGPUBindGroupLayout bindGroupLayout;

    std::vector<Star> stars;
    CameraUniforms cameraUniforms;
    
    // Camera state
    glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, -15.0f);
    float cameraRotation = 0.0f;
};