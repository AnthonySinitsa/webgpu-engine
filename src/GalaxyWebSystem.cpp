#include "GalaxyWebSystem.h"
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

GalaxyWebSystem::GalaxyWebSystem(WGPUDevice device) : device(device) {
    createPipeline();
    initStars();
    createBuffers();
    updateUniforms(); // Initial camera setup
}

GalaxyWebSystem::~GalaxyWebSystem() {
    cleanup();
}

void GalaxyWebSystem::cleanup() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
}

// MARK: initStars
void GalaxyWebSystem::initStars() {
    stars.resize(NUM_STARS);
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].position[0] = ((float)i - NUM_STARS/2.0f);  // x position along a line
        stars[i].position[1] = 0.0f;                       // y position at center
        stars[i].position[2] = 0.0f;                       // z position at center
    }
}

void GalaxyWebSystem::updateCamera(float deltaTime) {
    // Simple camera rotation
    cameraRotation += deltaTime * 0.5f;
    float radius = 10.0f;
    cameraPos.x = sin(cameraRotation) * radius;
    cameraPos.z = cos(cameraRotation) * radius;
    
    updateUniforms();
}

void GalaxyWebSystem::updateUniforms() {
    // Update view matrix
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Update projection matrix
    float aspect = 1280.0f / 720.0f; // TODO: Get actual window dimensions
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    
    // WebGPU uses Y-flipped NDC compared to OpenGL/Vulkan
    proj[1][1] *= -1;

    cameraUniforms.view = view;
    cameraUniforms.proj = proj;

    // Update uniform buffer
    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &cameraUniforms,
        sizeof(CameraUniforms)
    );
}

void GalaxyWebSystem::createBuffers() {
    // Vertex buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(Star) * stars.size();
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bufferDesc.mappedAtCreation = true;
    vertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    void* data = wgpuBufferGetMappedRange(vertexBuffer, 0, bufferDesc.size);
    memcpy(data, stars.data(), bufferDesc.size);
    wgpuBufferUnmap(vertexBuffer);

    // Uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(CameraUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
}

void GalaxyWebSystem::createPipeline() {
    // Create bind group layout for uniforms
    WGPUBindGroupLayoutEntry bglEntry = {};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.hasDynamicOffset = false;
    bglEntry.buffer.minBindingSize = sizeof(CameraUniforms);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Create the uniform buffer first
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(CameraUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Create bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(CameraUniforms);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    if (!bindGroup) {
        printf("Failed to create bind group!\n");
        return;
    }

    // Create shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
        struct CameraUniforms {
            view: mat4x4<f32>,
            proj: mat4x4<f32>,
        };

        @group(0) @binding(0) var<uniform> camera: CameraUniforms;

        struct VertexInput {
            @location(0) position: vec3f,
        };

        struct VertexOutput {
            @builtin(position) position: vec4f,
        };

        @vertex
        fn vs_main(in: VertexInput) -> VertexOutput {
            var out: VertexOutput;
            out.position = camera.proj * camera.view * vec4f(in.position, 1.0);
            return out;
        }

        @fragment
        fn fs_main() -> @location(0) vec4f {
            return vec4f(1.0, 0.8, 0.8, 1.0);  // White stars
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Vertex state
    WGPUVertexAttribute vertexAttrib = {};
    vertexAttrib.format = WGPUVertexFormat_Float32x3;
    vertexAttrib.offset = 0;
    vertexAttrib.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Star);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &vertexAttrib;

    // Pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;

    // Vertex state
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    // Fragment state
    WGPUFragmentState fragmentState = {};
    WGPUBlendState blend = {};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha = blend.color;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Other states
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void GalaxyWebSystem::render(WGPURenderPassEncoder renderPass) {
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, sizeof(Star) * stars.size());
    wgpuRenderPassEncoderDraw(renderPass, NUM_STARS, 1, 0, 0);
}