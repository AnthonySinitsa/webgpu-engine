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
    ~GalaxyWebSystem();  // Add destructor declaration

    void render(WGPURenderPassEncoder renderPass);
    void updateCamera(float deltaTime);  // Add updateCamera declaration

private:
    static constexpr int NUM_STARS = 10;
    
    void createPipelineAndResources();
    void createBuffers();
    void createBindGroup(); 
    void initStars();
    void updateUniforms();

    WGPUDevice device;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    std::vector<Star> stars;
    CameraUniforms cameraUniforms;
    float cameraRotation = 0.0f;
};