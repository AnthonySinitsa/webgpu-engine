#pragma once

#include <webgpu/webgpu.h>
#include <vector>
#include <memory>

struct Star {
    float position[3];  // x, y, z position
};

class GalaxyWebSystem {
public:
    GalaxyWebSystem(WGPUDevice device);
    ~GalaxyWebSystem();

    void render(WGPURenderPassEncoder renderPass);
    void cleanup();

private:
    static constexpr int NUM_STARS = 10;
    
    void createBuffers();
    void createPipeline();
    void initStars();

    WGPUDevice device;
    WGPUBuffer vertexBuffer;
    WGPURenderPipeline pipeline;
    WGPUBindGroup bindGroup;
    WGPUBindGroupLayout bindGroupLayout;

    std::vector<Star> stars;
};