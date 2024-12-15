# Building for desktop (WebGPU-native) with Dawn:
#  1. git clone https://github.com/google/dawn dawn
#  2. cmake -B build -DIMGUI_DAWN_DIR=dawn
#  3. cmake --build build
# The resulting binary will be found at one of the following locations:
#   * build/Debug/example_glfw_wgpu[.exe]
#   * build/example_glfw_wgpu[.exe]

# Building for Emscripten:
#  1. Install Emscripten SDK following the instructions: https://emscripten.org/docs/getting_started/downloads.html
#  2. Install Ninja build system
#  3. emcmake cmake -G Ninja -B build
#  3. cmake --build build
#  4. emrun build/index.html

cmake_minimum_required(VERSION 3.10.2)
project(imgui_example_glfw_wgpu C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)

# Dear ImGui
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

if(EMSCRIPTEN_VERSION VERSION_GREATER_EQUAL "3.1.57")
  set(IMGUI_EMSCRIPTEN_GLFW3 "--use-port=contrib.glfw3" CACHE STRING "Choose between --use-port=contrib.glfw3 and -sUSE_GLFW=3 for GLFW implementation")
else()
  set(IMGUI_EMSCRIPTEN_GLFW3 "-sUSE_GLFW=3" CACHE STRING "Use -sUSE_GLFW=3 for GLFW implementation" FORCE)
endif()

add_executable(example_glfw_wgpu
  main.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_wgpu.cpp
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_demo.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
)

target_include_directories(example_glfw_wgpu PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

# Emscripten settings
if("${IMGUI_EMSCRIPTEN_GLFW3}" STREQUAL "--use-port=contrib.glfw3")
  target_compile_options(example_glfw_wgpu PUBLIC
      "${IMGUI_EMSCRIPTEN_GLFW3}"
  )
endif()

target_compile_options(example_glfw_wgpu PUBLIC
  -sDISABLE_EXCEPTION_CATCHING=1
  -DIMGUI_DISABLE_FILE_FUNCTIONS=1
)

target_link_options(example_glfw_wgpu PRIVATE
  "-sUSE_WEBGPU=1"
  "${IMGUI_EMSCRIPTEN_GLFW3}"
  "-sWASM=1"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sNO_EXIT_RUNTIME=0"
  "-sASSERTIONS=1"
  "-sDISABLE_EXCEPTION_CATCHING=1"
  "-sNO_FILESYSTEM=1"
)

set_target_properties(example_glfw_wgpu PROPERTIES OUTPUT_NAME "index")

# Copy index.html to build directory
add_custom_command(TARGET example_glfw_wgpu POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" $<TARGET_FILE_DIR:example_glfw_wgpu>
)