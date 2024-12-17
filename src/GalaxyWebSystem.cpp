#include "GalaxyWebSystem.h"
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

GalaxyWebSystem::GalaxyWebSystem(WGPUDevice device) : device(device) {
    if (!device) {
        printf("Invalid device provided to GalaxyWebSystem!\n");
        return;
    }
    
    printf("Initializing GalaxyWebSystem...\n");
    
    // First initialize the stars data
    initStars();
    printf("Stars initialized, count: %zu\n", stars.size());
    
    // Create pipeline and related resources
    createPipelineAndResources();
    printf("GalaxyWebSystem initialization complete\n");
}

GalaxyWebSystem::~GalaxyWebSystem() {
    if (vertexBuffer) wgpuBufferRelease(vertexBuffer);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (pipeline) wgpuRenderPipelineRelease(pipeline);
    if (bindGroup) wgpuBindGroupRelease(bindGroup);
    if (bindGroupLayout) wgpuBindGroupLayoutRelease(bindGroupLayout);
}


void GalaxyWebSystem::createPipelineAndResources() {
    // Create bind group layout first
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
    if (!bindGroupLayout) {
        printf("Failed to create bind group layout!\n");
        return;
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    if (!pipelineLayout) {
        printf("Failed to create pipeline layout!\n");
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
            return vec4f(1.0, 1.0, 1.0, 1.0);  // White points
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) {
        printf("Failed to create shader module!\n");
        wgpuPipelineLayoutRelease(pipelineLayout);
        return;
    }

    // Set up vertex buffer layout
    WGPUVertexAttribute vertexAttrib = {};
    vertexAttrib.format = WGPUVertexFormat_Float32x3;
    vertexAttrib.offset = 0;
    vertexAttrib.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Star);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &vertexAttrib;

    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    fragment.targets = &colorTarget;
    pipelineDesc.fragment = &fragment;

    WGPUPrimitiveState primitive = {};
    primitive.topology = WGPUPrimitiveTopology_PointList;
    primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    primitive.frontFace = WGPUFrontFace_CCW;
    primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.primitive = primitive;

    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample = multisample;

    pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline) {
        printf("Failed to create render pipeline!\n");
        return;
    }
    printf("Pipeline created successfully!\n");

    // Create buffers
    createBuffers();
    
    // Create bind group
    createBindGroup();
    
    // Initialize uniforms
    updateUniforms();
}


void GalaxyWebSystem::updateCamera(float deltaTime) {
    // Update camera rotation
    cameraRotation += deltaTime * 0.5f;
    
    // Calculate new camera position
    float radius = 15.0f;
    glm::vec3 cameraPos = glm::vec3(
        sin(cameraRotation) * radius,
        5.0f,
        cos(cameraRotation) * radius
    );
    
    // Update view matrix
    glm::mat4 view = glm::lookAt(
        cameraPos,                    // Camera position
        glm::vec3(0.0f),             // Look at center
        glm::vec3(0.0f, 1.0f, 0.0f)  // Up vector
    );
    
    cameraUniforms.view = view;

    // Update uniform buffer
    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &cameraUniforms,
        sizeof(CameraUniforms)
    );
}


void GalaxyWebSystem::initStars() {
    stars.clear();
    stars.resize(NUM_STARS);
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].position[0] = ((float)i - NUM_STARS/2.0f);  // x position along a line
        stars[i].position[1] = 0.0f;                         // y position at center
        stars[i].position[2] = 0.0f;                         // z position at center
    }
    printf("Stars initialized with %d points\n", NUM_STARS);
}

void GalaxyWebSystem::createBuffers() {
    // Create vertex buffer
    const size_t vertexBufferSize = sizeof(Star) * stars.size();
    printf("Creating vertex buffer of size %zu\n", vertexBufferSize);
    
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = vertexBufferSize;
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.mappedAtCreation = true;
    vertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    
    if (!vertexBuffer) {
        printf("Failed to create vertex buffer!\n");
        return;
    }

    // Map and write vertex data
    void* vertexData = wgpuBufferGetMappedRange(vertexBuffer, 0, vertexBufferSize);
    if (!vertexData) {
        printf("Failed to map vertex buffer!\n");
        return;
    }
    memcpy(vertexData, stars.data(), vertexBufferSize);
    wgpuBufferUnmap(vertexBuffer);
    
    printf("Vertex buffer created and populated successfully\n");

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(CameraUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;  // We'll update this using queue.writeBuffer
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);
    
    if (!uniformBuffer) {
        printf("Failed to create uniform buffer!\n");
        return;
    }
    
    printf("Uniform buffer created successfully. Size: %zu\n", sizeof(CameraUniforms));
}


void GalaxyWebSystem::createBindGroup() {
    if (!bindGroupLayout || !uniformBuffer) {
        printf("Cannot create bind group: missing required resources!\n");
        return;
    }

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
    printf("Bind group created successfully!\n");
}


void GalaxyWebSystem::updateUniforms() {
    // Set up view matrix
    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 5.0f, -15.0f),  // Camera position
        glm::vec3(0.0f, 0.0f, 0.0f),    // Look at center
        glm::vec3(0.0f, 1.0f, 0.0f)     // Up vector
    );
    
    // Set up projection matrix
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f),    // FOV
        1280.0f / 720.0f,       // Aspect ratio
        0.1f,                   // Near
        100.0f                  // Far
    );

    // Flip Y for WebGPU coordinate system
    proj[1][1] *= -1;

    // Update our uniform data
    cameraUniforms.view = view;
    cameraUniforms.proj = proj;

    // Write to uniform buffer
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    if (!queue) {
        printf("Failed to get device queue!\n");
        return;
    }

    printf("Writing uniform data of size %zu to buffer\n", sizeof(CameraUniforms));
    wgpuQueueWriteBuffer(
        queue,
        uniformBuffer,
        0,
        &cameraUniforms,
        sizeof(CameraUniforms)
    );
    printf("Uniform data written successfully\n");
}

void GalaxyWebSystem::render(WGPURenderPassEncoder renderPass) {
    if (!pipeline || !bindGroup || !vertexBuffer) {
        printf("Missing required resources for rendering!\n");
        return;
    }

    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(renderPass, NUM_STARS, 1, 0, 0);
    
    printf("Render pass commands encoded\n");
}