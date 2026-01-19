cmake_minimum_required(VERSION 3.20)
project(SandSim)

set(CMAKE_CXX_STANDARD 17)

# 1. Find system libraries
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

# 2. Include directories
include_directories(
        ${OPENGL_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIRS}
        imgui   # Include the local imgui folder
)

# 3. Gather source files
file(GLOB IMGUI_SOURCES "imgui/*.cpp")

# 4. Create Executable
add_executable(SandSim main.cpp ${IMGUI_SOURCES})

# 5. Link Libraries
target_link_libraries(SandSim
        OpenGL::GL
        GLEW::GLEW
        glfw
)