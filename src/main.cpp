// Dear ImGui: standalone example application for using GLFW + WebGPU
// - Emscripten is supported for publishing on web. See https://emscripten.org.
// - Dawn is used as a WebGPU implementation on desktop.

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include "imgui.h"
#include "../external/imgui/backends/imgui_impl_glfw.h"
#include "../external/imgui/backends/imgui_impl_wgpu.h"
#include "../external/imgui/imgui_internal.h"
// #include "GalaxyWebSystem.h"
#include "TriangleRenderer.h"
#include "Camera.h"
#include "GridRenderer.h"
#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#else
#include <webgpu/webgpu_glfw.h>
#endif

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

// This example can also compile and run with Emscripten! See 'Makefile.emscripten' for details.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

// Global WebGPU required states
static WGPUInstance      wgpu_instance = nullptr;
static WGPUDevice        wgpu_device = nullptr;
static WGPUSurface       wgpu_surface = nullptr;
static WGPUTextureFormat wgpu_preferred_fmt = WGPUTextureFormat_RGBA8Unorm;
static WGPUSwapChain     wgpu_swap_chain = nullptr;
static int               wgpu_swap_chain_width = 1280;
static int               wgpu_swap_chain_height = 720;

// static std::unique_ptr<GalaxyWebSystem> galaxy_system = nullptr;
static std::unique_ptr<TriangleRenderer> triangle_renderer = nullptr;
static std::unique_ptr<GridRenderer> grid_renderer = nullptr;

static Camera camera{};
static struct CameraState {
    glm::vec3 position{0.0f, -1.5f, -3.0f};
    glm::vec3 rotation{-0.45f, 0.0f, 0.0f};
    float fov = 45.0f;
    float aspectRatio = 1280.0f / 720.0f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
} cameraState;

static bool opt_fullscreen = true;
static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

// Forward declarations
static bool InitWGPU(GLFWwindow* window);
static void CreateSwapChain(int width, int height);


static void renderCameraControls() {
    bool cameraUpdated = false;
    
    if (ImGui::CollapsingHeader("Camera Controls")) {
        // Position controls
        if (ImGui::DragFloat3("Position", &cameraState.position.x, 0.1f)) {
            cameraUpdated = true;
        }
        
        // Rotation controls (in degrees for easier understanding)
        glm::vec3 rotationDegrees = glm::degrees(cameraState.rotation);
        if (ImGui::DragFloat3("Rotation", &rotationDegrees.x, 1.0f)) {
            cameraState.rotation = glm::radians(rotationDegrees);
            cameraUpdated = true;
        }
        
        // Projection controls
        if (ImGui::SliderFloat("FOV", &cameraState.fov, 1.0f, 120.0f)) {
            cameraUpdated = true;
        }
        
        if (ImGui::DragFloat("Near Clip", &cameraState.nearClip, 0.1f, 0.1f, cameraState.farClip)) {
            cameraUpdated = true;
        }
        
        if (ImGui::DragFloat("Far Clip", &cameraState.farClip, 0.1f, cameraState.nearClip, 1000.0f)) {
            cameraUpdated = true;
        }

        if (cameraUpdated) {
            camera.setPerspectiveProjection(
                glm::radians(cameraState.fov),
                cameraState.aspectRatio,
                cameraState.nearClip,
                cameraState.farClip
            );
            camera.setViewYXZ(cameraState.position, cameraState.rotation);
        }
    }
}


void createDockspace() {
    // Configure flags
    dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;

    if (opt_fullscreen) {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | 
                       ImGuiWindowFlags_NoBringToFrontOnFocus | 
                       ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    }

    // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", nullptr, window_flags);
    ImGui::PopStyleVar();

    if (opt_fullscreen)
        ImGui::PopStyleVar(2);

    // Submit the DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

        // Set up default layout if not already done
        static bool first_time = true;
        if (first_time) {
            first_time = false;
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

            auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, nullptr, &dockspace_id);
            ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }

    ImGui::End();
}


static void glfw_error_callback(int error, const char* description)
{
    printf("GLFW Error %d: %s\n", error, description);
}

static void wgpu_error_callback(WGPUErrorType error_type, const char* message, void*)
{
    const char* error_type_lbl = "";
    switch (error_type)
    {
    case WGPUErrorType_Validation:  error_type_lbl = "Validation"; break;
    case WGPUErrorType_OutOfMemory: error_type_lbl = "Out of memory"; break;
    case WGPUErrorType_Unknown:     error_type_lbl = "Unknown"; break;
    case WGPUErrorType_DeviceLost:  error_type_lbl = "Device lost"; break;
    default:                        error_type_lbl = "Unknown";
    }
    printf("%s error: %s\n", error_type_lbl, message);
}

// MARK: Main code
int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Make sure GLFW does not initialize any graphics context.
    // This needs to be done explicitly later.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(wgpu_swap_chain_width, wgpu_swap_chain_height, "Dear ImGui GLFW+WebGPU example", nullptr, nullptr);
    if (window == nullptr)
        return 1;

    // Initialize the WebGPU environment
    if (!InitWGPU(window))
    {
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    CreateSwapChain(wgpu_swap_chain_width, wgpu_swap_chain_height);
    glfwShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOther(window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = wgpu_device;
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = wgpu_preferred_fmt;
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // - Emscripten allows preloading a file or folder to be accessible at runtime. See Makefile for details.
    //io.Fonts->AddFontDefault();
#ifndef IMGUI_DISABLE_FILE_FUNCTIONS
    //io.Fonts->AddFontFromFileTTF("fonts/segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("fonts/ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);
#endif

    // Our state
    bool show_demo_window = false;
    ImVec4 clear_color = ImVec4(0.f, 0.f, 0.f, 1.00f);

    // Initialize camera
    camera.setPerspectiveProjection(
        glm::radians(cameraState.fov),
        cameraState.aspectRatio,
        cameraState.nearClip,
        cameraState.farClip
    );
    camera.setViewYXZ(cameraState.position, cameraState.rotation);

    // Main loop
#ifdef __EMSCRIPTEN__
    // For an Emscripten build we are disabling file-system access, so let's not attempt to do a fopen() of the imgui.ini file.
    // You may manually call LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;
    EMSCRIPTEN_MAINLOOP_BEGIN
#else
    while (!glfwWindowShouldClose(window))
#endif
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // React to changes in screen size
        int width, height;
        glfwGetFramebufferSize((GLFWwindow*)window, &width, &height);
        if (width != wgpu_swap_chain_width || height != wgpu_swap_chain_height)
        {
            ImGui_ImplWGPU_InvalidateDeviceObjects();
            CreateSwapChain(width, height);
            ImGui_ImplWGPU_CreateDeviceObjects();

            // Update camera aspect ratio
            cameraState.aspectRatio = float(width) / float(height);
            camera.setPerspectiveProjection(
                glm::radians(cameraState.fov),
                cameraState.aspectRatio,
                cameraState.nearClip,
                cameraState.farClip
            );
        }

        // MARK: ImGui
        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        createDockspace();

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        {
            ImGui::Begin("Hierarchy");

            ImGui::Checkbox("Demo Window", &show_demo_window);

            ImGui::ColorEdit3("clear color", (float*)&clear_color);
            
            ImGui::Separator();
            renderCameraControls();
            ImGui::Separator();

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // Rendering
        ImGui::Render();

#ifndef __EMSCRIPTEN__
        // Tick needs to be called in Dawn to display validation errors
        // wgpuDeviceTick(wgpu_device);
#endif

        WGPURenderPassColorAttachment color_attachments = {};
        color_attachments.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_attachments.loadOp = WGPULoadOp_Clear;
        color_attachments.storeOp = WGPUStoreOp_Store;
        color_attachments.clearValue = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        color_attachments.view = wgpuSwapChainGetCurrentTextureView(wgpu_swap_chain);

        WGPURenderPassDescriptor render_pass_desc = {};
        render_pass_desc.colorAttachmentCount = 1;
        render_pass_desc.colorAttachments = &color_attachments;
        render_pass_desc.depthStencilAttachment = nullptr;

        WGPUCommandEncoderDescriptor enc_desc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wgpu_device, &enc_desc);

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &render_pass_desc);

        // MARK: galaxy
        // galaxy_system->updateCamera(deltaTime);
        // galaxy_system->render(pass);
        float deltaTime = ImGui::GetIO().DeltaTime;
        grid_renderer->render(pass, camera);
        triangle_renderer->update(deltaTime);
        triangle_renderer->render(pass, camera);

        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBufferDescriptor cmd_buffer_desc = {};
        WGPUCommandBuffer cmd_buffer = wgpuCommandEncoderFinish(encoder, &cmd_buffer_desc);
        WGPUQueue queue = wgpuDeviceGetQueue(wgpu_device);
        wgpuQueueSubmit(queue, 1, &cmd_buffer);

#ifndef __EMSCRIPTEN__
        wgpuSwapChainPresent(wgpu_swap_chain);
#endif

        wgpuTextureViewRelease(color_attachments.view);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuCommandBufferRelease(cmd_buffer);
    }
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_MAINLOOP_END;
#endif

    triangle_renderer.reset();

    // Cleanup
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

#ifndef __EMSCRIPTEN__
static WGPUAdapter RequestAdapter(WGPUInstance instance)
{
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* pUserData)
    {
        if (status == WGPURequestAdapterStatus_Success)
            *(WGPUAdapter*)(pUserData) = adapter;
        else
            printf("Could not get WebGPU adapter: %s\n", message);
};
    WGPUAdapter adapter;
    wgpuInstanceRequestAdapter(instance, nullptr, onAdapterRequestEnded, (void*)&adapter);
    return adapter;
}

static WGPUDevice RequestDevice(WGPUAdapter& adapter)
{
    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* pUserData)
    {
        if (status == WGPURequestDeviceStatus_Success)
            *(WGPUDevice*)(pUserData) = device;
        else
            printf("Could not get WebGPU device: %s\n", message);
    };
    WGPUDevice device;
    wgpuAdapterRequestDevice(adapter, nullptr, onDeviceRequestEnded, (void*)&device);
    return device;
}
#endif

// MARK: InitWGPU
static bool InitWGPU(GLFWwindow* window)
{
    wgpu::Instance instance = wgpuCreateInstance(nullptr);

#ifdef __EMSCRIPTEN__
    wgpu_device = emscripten_webgpu_get_device();
    if (!wgpu_device)
        return false;
#else
    WGPUAdapter adapter = RequestAdapter(instance.Get());
    if (!adapter)
        return false;
    wgpu_device = RequestDevice(adapter);
#endif

#ifdef __EMSCRIPTEN__
    wgpu::SurfaceDescriptorFromCanvasHTMLSelector html_surface_desc = {};
    html_surface_desc.selector = "#canvas";
    wgpu::SurfaceDescriptor surface_desc = {};
    surface_desc.nextInChain = &html_surface_desc;
    wgpu::Surface surface = instance.CreateSurface(&surface_desc);

    wgpu::Adapter adapter = {};
    wgpu_preferred_fmt = (WGPUTextureFormat)surface.GetPreferredFormat(adapter);
#else
    wgpu::Surface surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);
    if (!surface)
        return false;
    wgpu_preferred_fmt = WGPUTextureFormat_BGRA8Unorm;
#endif

    wgpu_instance = instance.MoveToCHandle();
    wgpu_surface = surface.MoveToCHandle();

    wgpuDeviceSetUncapturedErrorCallback(wgpu_device, wgpu_error_callback, nullptr);

    // galaxy_system = std::make_unique<GalaxyWebSystem>(wgpu_device);
    // if (!galaxy_system) {
    //     printf("Failed to create galaxy system!\n");
    //     return false;
    // }
    triangle_renderer = std::make_unique<TriangleRenderer>(wgpu_device);
    if (!triangle_renderer) {
        printf("Failed to create triangle renderer!\n");
        return false;
    }
    grid_renderer = std::make_unique<GridRenderer>(wgpu_device);
    if (!grid_renderer) {
        printf("Failed to create grid renderer!\n");
        return false;
    }

    return true;
}

static void CreateSwapChain(int width, int height)
{
    if (wgpu_swap_chain)
        wgpuSwapChainRelease(wgpu_swap_chain);
    wgpu_swap_chain_width = width;
    wgpu_swap_chain_height = height;
    WGPUSwapChainDescriptor swap_chain_desc = {};
    swap_chain_desc.usage = WGPUTextureUsage_RenderAttachment;
    swap_chain_desc.format = wgpu_preferred_fmt;
    swap_chain_desc.width = width;
    swap_chain_desc.height = height;
    swap_chain_desc.presentMode = WGPUPresentMode_Fifo;
    wgpu_swap_chain = wgpuDeviceCreateSwapChain(wgpu_device, wgpu_surface, &swap_chain_desc);
}
