cmake_minimum_required(VERSION 3.20)
project(SandSim)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

include_directories(
        ${OPENGL_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIRS}
        imgui
        ${CMAKE_SOURCE_DIR}  # <--- ADD THIS (Points to project root)
)

file(GLOB IMGUI_SOURCES "imgui/*.cpp")

add_executable(SandSim main.cpp ${IMGUI_SOURCES})

target_link_libraries(SandSim
        OpenGL::GL
        GLEW::GLEW
        glfw
        # No GLM library needed here, it's header-only!
)