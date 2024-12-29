#include "PointWebSystem.h"
#include <cstring>
#include <iostream>

PointWebSystem::PointWebSystem(WGPUDevice device) : device(device) {
    initPoints();
    createBuffers();
    createPipelineAndResources();
    createComputePipeline();
    createBindGroups();
}

PointWebSystem::~PointWebSystem() {
    if (vertexBufferA) wgpuBufferRelease(vertexBufferA);
    if (vertexBufferB) wgpuBufferRelease(vertexBufferB);
    if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
    if (renderPipeline) wgpuRenderPipelineRelease(renderPipeline);
    if (computePipeline) wgpuComputePipelineRelease(computePipeline);
    if (renderBindGroup) wgpuBindGroupRelease(renderBindGroup);
    if (computeBindGroupA) wgpuBindGroupRelease(computeBindGroupA);
    if (computeBindGroupB) wgpuBindGroupRelease(computeBindGroupB);
    if (renderBindGroupLayout) wgpuBindGroupLayoutRelease(renderBindGroupLayout);
    if (computeBindGroupLayout) wgpuBindGroupLayoutRelease(computeBindGroupLayout);
    if (ellipseBuffer) wgpuBufferRelease(ellipseBuffer);
}

// MARK: initPoints
void PointWebSystem::initPoints() {
    points.resize(NUM_POINTS);
    
    int starsPerEllipse = NUM_POINTS / MAX_ELLIPSES;
    float currentEllipseSize = 1.83f; // Base radius from galaxy system
    float tiltIncrement = 0.16f;      // From galaxy system
    
    for (int ellipseIndex = 0; ellipseIndex < MAX_ELLIPSES; ellipseIndex++) {
        int startIndex = ellipseIndex * starsPerEllipse;
        int endIndex = (ellipseIndex == MAX_ELLIPSES - 1) ? NUM_POINTS : startIndex + starsPerEllipse;
        int starsInThisEllipse = endIndex - startIndex;

        float angleStep = (2.0f * 3.14159f) / starsInThisEllipse;
        float currentTilt = ellipseIndex * tiltIncrement;

        for (int i = startIndex; i < endIndex; i++) {
            float t = (i - startIndex) * angleStep;
            
            // Base position calculation
            float x = currentEllipseSize * cos(t) * cos(currentTilt);
            float z = currentEllipseSize * cos(t) * sin(currentTilt);
            
            // Calculate height using rough approximation of de Vaucouleurs's Law
            float radius = sqrt(x * x + z * z) + 0.0001f;
            float baseHeight = 0.5f * exp(-1.4f * pow(radius/3.66f, 0.25f));
            float randomizedHeight = baseHeight * (hash(i) * 2.0f - 1.0f);

            // Random offset for more natural distribution
            float randRadius = hash(i * 12.345f) * currentEllipseSize;
            float randAngle = hash(i * 67.890f) * 2.0f * 3.14159f;
            
            // Calculate offsets
            float offsetX = randRadius * cos(randAngle);
            float offsetZ = randRadius * sin(randAngle);

            // Set final position
            points[i].position[0] = x + offsetX;
            points[i].position[1] = randomizedHeight;
            points[i].position[2] = z + offsetZ;

            // Store parameters in velocity for compute shader
            points[i].velocity[0] = t;                // angle
            points[i].velocity[1] = randomizedHeight; // stored height
            points[i].velocity[2] = randRadius;       // radial offset
        }

        currentEllipseSize += 0.5f; // Increment size for next ellipse
    }
}

// Helper function for hash (used in initialization)
float PointWebSystem::hash(uint32_t n) {
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 0x789221U) + 0x137631U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

void PointWebSystem::createPipelineAndResources() {
    // Create render bind group layout first
    WGPUBindGroupLayoutEntry bglEntry = {};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.hasDynamicOffset = false;
    bglEntry.buffer.minBindingSize = sizeof(UniformData);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    
    renderBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    printf("Created render bind group layout: %p\n", (void*)renderBindGroupLayout);

    // Create bind group
    if (uniformBuffer && renderBindGroupLayout) {
        WGPUBindGroupEntry bgEntry = {};
        bgEntry.binding = 0;
        bgEntry.buffer = uniformBuffer;
        bgEntry.offset = 0;
        bgEntry.size = sizeof(UniformData);

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = renderBindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;

        printf("Before creating bind group. Layout: %p, Buffer: %p\n", 
               (void*)renderBindGroupLayout, (void*)uniformBuffer);
        renderBindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
        printf("After creating bind group: %p\n", (void*)renderBindGroup);
    } else {
        printf("Failed preconditions for bind group. Layout: %p, Buffer: %p\n",
               (void*)renderBindGroupLayout, (void*)uniformBuffer);
    }

    // Create shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = R"(
            struct Uniforms {
                viewProj: mat4x4<f32>,
            }
            @binding(0) @group(0) var<uniform> uniforms: Uniforms;

            struct VertexInput {
                @location(0) position: vec3f,
            };

            struct VertexOutput {
                @builtin(position) position: vec4f,
            };

            @vertex
            fn vs_main(in: VertexInput) -> VertexOutput {
                var out: VertexOutput;
                let worldPos = vec4f(in.position, 1.0);
                out.position = uniforms.viewProj * worldPos;
                return out;
            }

            @fragment
            fn fs_main() -> @location(0) vec4f {
                // Fixed white color for testing
                return vec4f(1.0, 1.0, 1.0, 1.0);
            }
        )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor layoutDesc = {};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &renderBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    // Set up vertex attributes and buffer layout
    WGPUVertexAttribute attribute = {};
    attribute.format = WGPUVertexFormat_Float32x3;
    attribute.offset = offsetof(Point, position);
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = {};
    vertexBufferLayout.arrayStride = sizeof(Point);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &attribute;

    // Fragment state
    WGPUBlendState blend = {};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha = blend.color;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexBufferLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.layout = pipelineLayout;

    WGPUMultisampleState multisample = {};
    multisample.count = 1;
    multisample.mask = 0xFFFFFFFF;
    multisample.alphaToCoverageEnabled = false;
    pipelineDesc.multisample = multisample;

    renderPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Cleanup
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}


void PointWebSystem::createComputePipeline() {
    // First create the compute bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};
    // Input buffer
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Compute;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    // Output buffer
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Compute;
    layoutEntries[1].buffer.type = WGPUBufferBindingType_Storage;
    // Ellipse parameters buffer
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Compute;
    layoutEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 3;
    bindGroupLayoutDesc.entries = layoutEntries;
    computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

    if (!computeBindGroupLayout) {
        printf("Failed to create compute bind group layout!\n");
        return;
    }

    // Now create the pipeline layout using the bind group layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &computeBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    if (!pipelineLayout) {
        printf("Failed to create compute pipeline layout!\n");
        return;
    }

    // Create compute shader
    WGPUShaderModuleWGSLDescriptor computeWGSLDesc = {};
    computeWGSLDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    computeWGSLDesc.code = R"(
        struct Point {
            @align(16) position: vec3f,
            @align(16) velocity: vec3f,
        }

        struct EllipseParams {
            majorAxis: f32,
            minorAxis: f32,
            tiltAngle: f32,
        }

        @group(0) @binding(0) var<storage, read> input: array<Point>;
        @group(0) @binding(1) var<storage, read_write> output: array<Point>;
        @group(0) @binding(2) var<storage, read> ellipses: array<EllipseParams>;

        const BASE_ROTATION_SPEED: f32 = -0.01;
        const SPEED_MULTIPLIER: f32 = 20.0;

        fn hash(n: u32) -> f32 {
            var nn = n;
            nn = (nn << 13u) ^ nn;
            nn = nn * (nn * nn * 15731u + 0x789221u) + 0x137631u;
            return f32(nn & 0x7fffffffu) / f32(0x7fffffff);
        }

        @compute @workgroup_size(256)
        fn main(@builtin(global_invocation_id) global_id : vec3u) {
            let index = global_id.x;
            if (index >= arrayLength(&input)) {
                return;
            }

            let starsPerEllipse = arrayLength(&input) / arrayLength(&ellipses);
            let ellipseIndex = min(index / starsPerEllipse, arrayLength(&ellipses) - 1);
            let params = ellipses[ellipseIndex];

            // Get stored parameters
            let currentAngle = input[index].velocity.x;
            let storedHeight = input[index].velocity.y;
            let radialOffset = input[index].velocity.z;

            // Calculate rotation speed based on ellipse size
            let speedFactor = SPEED_MULTIPLIER / max(params.majorAxis, 0.1);
            let rotationSpeed = BASE_ROTATION_SPEED * speedFactor;

            // Update angle
            var newAngle = currentAngle + rotationSpeed * 0.016;
            if (newAngle > 6.28318) {
                newAngle = newAngle - 6.28318;
            }

            // Calculate base ellipse position
            let x = params.majorAxis * cos(newAngle) * cos(params.tiltAngle) -
                    params.minorAxis * sin(newAngle) * sin(params.tiltAngle);
            let z = params.majorAxis * cos(newAngle) * sin(params.tiltAngle) +
                    params.minorAxis * sin(newAngle) * cos(params.tiltAngle);

            // Apply stored radial offset in orbital plane
            let offsetAngle = newAngle + radialOffset;
            let offset = vec3f(
                cos(offsetAngle) * radialOffset,
                0.0,
                sin(offsetAngle) * radialOffset
            );

            // Combine position with stored height
            let newPosition = vec3f(x, storedHeight, z) + offset;

            // Update the point
            output[index].position = newPosition;
            output[index].velocity = vec3f(newAngle, storedHeight, radialOffset);
        }
    )";

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&computeWGSLDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    if (!shaderModule) {
        printf("Failed to create compute shader module!\n");
        wgpuPipelineLayoutRelease(pipelineLayout);
        return;
    }

    // Create the compute pipeline
    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = "main";

    computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

    // Cleanup
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
}


void PointWebSystem::compute(WGPUComputePassEncoder computePass) {
    wgpuComputePassEncoderSetPipeline(computePass, computePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, 
        useBufferA ? computeBindGroupA : computeBindGroupB, 0, nullptr);
        
    // Calculate workgroup count to cover all points
    uint32_t workgroupCount = (NUM_POINTS + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    wgpuComputePassEncoderDispatchWorkgroups(computePass, workgroupCount, 1, 1);
}


void PointWebSystem::createBuffers() {
    // Create vertex buffers for double buffering
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(Point) * points.size();
    // Update usage flags to include read-only storage
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    // Create and initialize buffer A
    vertexBufferA = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    void* vertexDataA = wgpuBufferGetMappedRange(vertexBufferA, 0, vertexBufferDesc.size);
    memcpy(vertexDataA, points.data(), vertexBufferDesc.size);
    wgpuBufferUnmap(vertexBufferA);

    // Create buffer B (initially empty)
    vertexBufferB = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);
    void* vertexDataB = wgpuBufferGetMappedRange(vertexBufferB, 0, vertexBufferDesc.size);
    memcpy(vertexDataB, points.data(), vertexBufferDesc.size);
    wgpuBufferUnmap(vertexBufferB);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(UniformData);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformDesc.mappedAtCreation = false;
    uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Create ellipse parameters buffer
    WGPUBufferDescriptor ellipseBufferDesc = {};
    ellipseBufferDesc.size = sizeof(EllipseParams) * MAX_ELLIPSES;
    ellipseBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    ellipseBufferDesc.mappedAtCreation = true;
    
    ellipseBuffer = wgpuDeviceCreateBuffer(device, &ellipseBufferDesc);
    
    // Initialize ellipse parameters
    ellipseParams.resize(MAX_ELLIPSES);
    float currentRadius = 1.83f; // Base radius
    float tiltIncrement = 0.16f;
    
    for (int i = 0; i < MAX_ELLIPSES; i++) {
        ellipseParams[i].majorAxis = currentRadius;
        ellipseParams[i].minorAxis = currentRadius * 0.8f; // eccentricity of 0.8
        ellipseParams[i].tiltAngle = i * tiltIncrement;
        currentRadius += 0.5f;
    }
    
    // Copy ellipse parameters to buffer
    void* ellipseData = wgpuBufferGetMappedRange(ellipseBuffer, 0, ellipseBufferDesc.size);
    memcpy(ellipseData, ellipseParams.data(), ellipseBufferDesc.size);
    wgpuBufferUnmap(ellipseBuffer);
}

void PointWebSystem::createBindGroups() {
    // First create render bind group (this part remains unchanged)
    if (!renderBindGroupLayout) {
        printf("Error: renderBindGroupLayout is null!\n");
        return;
    }

    WGPUBindGroupEntry renderEntry = {};
    renderEntry.binding = 0;
    renderEntry.buffer = uniformBuffer;
    renderEntry.offset = 0;
    renderEntry.size = sizeof(UniformData);

    WGPUBindGroupDescriptor renderBgDesc = {};
    renderBgDesc.layout = renderBindGroupLayout;
    renderBgDesc.entryCount = 1;
    renderBgDesc.entries = &renderEntry;

    renderBindGroup = wgpuDeviceCreateBindGroup(device, &renderBgDesc);
    if (!renderBindGroup) {
        printf("Failed to create render bind group!\n");
        return;
    }

    // Create compute bind group layout first
    WGPUBindGroupLayoutEntry computeEntries[3] = {};
    // Input buffer
    computeEntries[0].binding = 0;
    computeEntries[0].visibility = WGPUShaderStage_Compute;
    computeEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    // Output buffer
    computeEntries[1].binding = 1;
    computeEntries[1].visibility = WGPUShaderStage_Compute;
    computeEntries[1].buffer.type = WGPUBufferBindingType_Storage;
    // Ellipse parameters buffer
    computeEntries[2].binding = 2;
    computeEntries[2].visibility = WGPUShaderStage_Compute;
    computeEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor computeBglDesc = {};
    computeBglDesc.entryCount = 3;
    computeBglDesc.entries = computeEntries;
    computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &computeBglDesc);

    // Create bind groups for compute shader
    {
        WGPUBindGroupEntry entriesA[3] = {};
        // Input buffer A
        entriesA[0].binding = 0;
        entriesA[0].buffer = vertexBufferA;
        entriesA[0].offset = 0;
        entriesA[0].size = sizeof(Point) * NUM_POINTS;
        // Output buffer B
        entriesA[1].binding = 1;
        entriesA[1].buffer = vertexBufferB;
        entriesA[1].offset = 0;
        entriesA[1].size = sizeof(Point) * NUM_POINTS;
        // Ellipse buffer
        entriesA[2].binding = 2;
        entriesA[2].buffer = ellipseBuffer;
        entriesA[2].offset = 0;
        entriesA[2].size = sizeof(EllipseParams) * MAX_ELLIPSES;

        WGPUBindGroupDescriptor bgDescA = {};
        bgDescA.layout = computeBindGroupLayout;
        bgDescA.entryCount = 3;
        bgDescA.entries = entriesA;
        computeBindGroupA = wgpuDeviceCreateBindGroup(device, &bgDescA);
    }

    // Create second bind group with swapped buffers
    {
        WGPUBindGroupEntry entriesB[3] = {};
        // Input buffer B
        entriesB[0].binding = 0;
        entriesB[0].buffer = vertexBufferB;
        entriesB[0].offset = 0;
        entriesB[0].size = sizeof(Point) * NUM_POINTS;
        // Output buffer A
        entriesB[1].binding = 1;
        entriesB[1].buffer = vertexBufferA;
        entriesB[1].offset = 0;
        entriesB[1].size = sizeof(Point) * NUM_POINTS;
        // Ellipse buffer
        entriesB[2].binding = 2;
        entriesB[2].buffer = ellipseBuffer;
        entriesB[2].offset = 0;
        entriesB[2].size = sizeof(EllipseParams) * MAX_ELLIPSES;

        WGPUBindGroupDescriptor bgDescB = {};
        bgDescB.layout = computeBindGroupLayout;
        bgDescB.entryCount = 3;
        bgDescB.entries = entriesB;
        computeBindGroupB = wgpuDeviceCreateBindGroup(device, &bgDescB);
    }
}


void PointWebSystem::updateUniforms(const Camera& camera) {
    uniformData.viewProj = camera.getProjection() * camera.getView();
    
    wgpuQueueWriteBuffer(
        wgpuDeviceGetQueue(device),
        uniformBuffer,
        0,
        &uniformData,
        sizeof(UniformData)
    );
}

void PointWebSystem::render(WGPURenderPassEncoder renderPass, const Camera& camera) {
    updateUniforms(camera);
    
    wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, renderBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, 
        useBufferA ? vertexBufferA : vertexBufferB, 0, sizeof(Point) * points.size());
    wgpuRenderPassEncoderDraw(renderPass, NUM_POINTS, 1, 0, 0);

    // Toggle buffers for next frame
    useBufferA = !useBufferA;
}