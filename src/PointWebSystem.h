#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "Camera.h"

struct Point {
    alignas(16) float position[3];  // x, y, z position
    alignas(16) float velocity[3];  // x, y, z velocity
};

struct UniformData {
    alignas(16) glm::mat4 viewProj;
};

class PointWebSystem {
public:
    static constexpr int NUM_POINTS = 100000;
    static constexpr int WORKGROUP_SIZE = 256;
    static constexpr int MAX_ELLIPSES = 30;

    PointWebSystem(WGPUDevice device);
    ~PointWebSystem();

    void render(WGPURenderPassEncoder renderPass, const Camera& camera);
    void compute(WGPUComputePassEncoder computePass);

private:
    static constexpr float POINT_SPACING = 1.0f;
    WGPUBuffer ellipseBuffer = nullptr;
    
    // Add structure for ellipse parameters
    struct EllipseParams {
        float majorAxis;
        float minorAxis;
        float tiltAngle;
    };
    std::vector<EllipseParams> ellipseParams;
    
    void createPipelineAndResources();
    void createComputePipeline();
    void createBuffers();
    void createBindGroups();
    void initPoints();
    float hash(uint32_t n);
    void updateUniforms(const Camera& camera);

    WGPUDevice device;
    
    // Graphics pipeline resources
    WGPUBuffer vertexBufferA = nullptr;
    WGPUBuffer vertexBufferB = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
    WGPUBindGroup renderBindGroup = nullptr;
    WGPUBindGroupLayout renderBindGroupLayout = nullptr;

    // Compute pipeline resources
    WGPUComputePipeline computePipeline = nullptr;
    WGPUBindGroup computeBindGroupA = nullptr;  // For buffer A -> B
    WGPUBindGroup computeBindGroupB = nullptr;  // For buffer B -> A
    WGPUBindGroupLayout computeBindGroupLayout = nullptr;

    bool useBufferA = true;  // Toggle between buffers
    std::vector<Point> points;
    UniformData uniformData;
};