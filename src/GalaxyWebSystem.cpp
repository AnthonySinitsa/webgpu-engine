#include "GalaxyWebSystem.h"
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

GalaxyWebSystem::GalaxyWebSystem(WGPUDevice device) : device(device) {
    createPipeline();
    initStars();
    createBuffers();
    updateUniforms();
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
    printf("\nInitializing %d stars:\n", NUM_STARS);
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].position[0] = ((float)i - NUM_STARS/2.0f) * 0.5f;
        stars[i].position[1] = 0.0f;
        stars[i].position[2] = 0.0f;

        printf("Star %d: pos(%.2f, %.2f, %.2f)\n", 
            i, 
            stars[i].position[0], 
            stars[i].position[1], 
            stars[i].position[2]);
    }
}

void GalaxyWebSystem::updateCamera(float deltaTime) {
    // Simple camera rotation
    // cameraRotation += deltaTime * 0.5f;
    // float radius = 10.0f;
    // cameraPos.x = sin(cameraRotation) * radius;
    // cameraPos.z = cos(cameraRotation) * radius;
    
    updateUniforms();
}

void GalaxyWebSystem::updateUniforms() {
    static int updateCount = 0;

    // Update view matrix
    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    
    // Update projection matrix
    float aspect = 1280.0f / 720.0f; // TODO: Get actual window dimensions
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), aspect, 0.1f, 100.0f);
    
    // WebGPU uses Y-flipped NDC compared to OpenGL/Vulkan
    proj[1][1] *= -1;

    if (updateCount++ % 60 == 0) {
        printf("\nCamera matrices (update %d):\n", updateCount);
        printf("View matrix:\n");
        for (int i = 0; i < 4; i++) {
            printf("[ %.2f %.2f %.2f %.2f ]\n", 
                view[i][0], view[i][1], view[i][2], view[i][3]);
        }
        printf("Projection matrix:\n");
        for (int i = 0; i < 4; i++) {
            printf("[ %.2f %.2f %.2f %.2f ]\n", 
                proj[i][0], proj[i][1], proj[i][2], proj[i][3]);
        }
    }

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

    printf("\nBuffer created with size: %llu bytes\n", bufferDesc.size);
    printf("Star data copied to buffer. First few positions:\n");
    Star* starData = (Star*)data;
    for (size_t i = 0; i < std::min(size_t(3), stars.size()); i++) {
        printf("Buffer Star %zu: pos(%.2f, %.2f, %.2f)\n", 
            i, 
            starData[i].position[0], 
            starData[i].position[1], 
            starData[i].position[2]);
    }

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
            @location(0) worldPos: vec3f,
        };

        @vertex
        fn vs_main(in: VertexInput) -> VertexOutput {
            var out: VertexOutput;
            let worldPos = vec4f(in.position, 1.0);
            out.position = camera.proj * camera.view * worldPos;
            out.worldPos = in.position;
            return out;
        }

        @fragment
        fn fs_main(@location(0) worldPos: vec3f) -> @location(0) vec4f {
            // Simple circular point
            let coord = vec2f(0.5);
            let dist = length(coord);
            
            // Simple point rendering
            if (dist > 0.5) {
                discard;
            }
            
            // Bright orange color
            return vec4f(1.0, 0.5, 0.0, 1.0);
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) {
        printf("Failed to create shader module!\n");
        return;
    }
    printf("Shader module created successfully!\n");

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

    // Add multisample state
    WGPUMultisampleState multisample = {};
    multisample.count = 1;  // Set to 1 for no multisampling
    multisample.mask = 0xFFFFFFFF;
    multisample.alphaToCoverageEnabled = false;
    pipelineDesc.multisample = multisample;

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

    // Primitive state
    WGPUPrimitiveState primitive = {};
    primitive.topology = WGPUPrimitiveTopology_PointList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.primitive = primitive;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!pipeline) {
        printf("Failed to create render pipeline!\n");
    } else {
        printf("Pipeline created successfully!\n");
    }

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}

void GalaxyWebSystem::render(WGPURenderPassEncoder renderPass) {
    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {  // Print every 60 frames to avoid spam
        printf("\nRender frame %d:\n", frameCount);
        printf("Star positions:\n");
        for (int i = 0; i < NUM_STARS; i++) {
            printf("Star %d: pos(%.2f, %.2f, %.2f)\n", 
                i, 
                stars[i].position[0], 
                stars[i].position[1], 
                stars[i].position[2]);
        }
        printf("Drawing %d stars\n", NUM_STARS);
        printf("Vertex buffer handle: %p\n", (void*)vertexBuffer);
        printf("Bind group handle: %p\n", (void*)bindGroup);
    }

    if (!pipeline || !vertexBuffer || !bindGroup) {
        printf("Error: Missing required resources for rendering!\n");
        printf("Pipeline: %p, VertexBuffer: %p, BindGroup: %p\n",
            (void*)pipeline, (void*)vertexBuffer, (void*)bindGroup);
        return;
    }

    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, sizeof(Star) * stars.size());

    if (frameCount % 60 == 0) {
        printf("Draw call: %d vertices\n", NUM_STARS);
    }

    wgpuRenderPassEncoderDraw(renderPass, NUM_STARS, 1, 0, 0);
}