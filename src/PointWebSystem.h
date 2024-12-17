#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Camera.h"

struct Point {
    float position[3];  // x, y, z position
};

struct UniformData {
    glm::mat4 viewProj;
};

class PointWebSystem {
public:
    PointWebSystem(WGPUDevice device);
    ~PointWebSystem();

    void render(WGPURenderPassEncoder renderPass, const Camera& camera);

private:
    static constexpr int NUM_POINTS = 10;
    static constexpr float POINT_SPACING = 1.0f;
    
    void createPipelineAndResources();
    void createBuffers();
    void createBindGroup();
    void initPoints();
    void updateUniforms(const Camera& camera);

    WGPUDevice device;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;

    std::vector<Point> points;
    UniformData uniformData;
};