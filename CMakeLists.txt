cmake_minimum_required(VERSION 3.20)
project(SandSim)

set(CMAKE_CXX_STANDARD 17)

# Load the Haply library and its submodules
add_subdirectory(Haply-API-cpp)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

include_directories(
        ${OPENGL_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIRS}
        imgui
        Haply-API-cpp/src
        # --- FIX: Add the path to the Serial library headers ---
        Haply-API-cpp/external/github-HaplyHaptics-serial/include
)

file(GLOB IMGUI_SOURCES "imgui/*.cpp")

add_executable(SandSim main.cpp ${IMGUI_SOURCES} haply_test.cpp)

target_link_libraries(SandSim
        OpenGL::GL
        GLEW::GLEW
        glfw
        haply-api-cpp
)