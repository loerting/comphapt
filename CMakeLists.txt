cmake_minimum_required(VERSION 3.20)
project(SandSim)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

include_directories(serial/include)

set(SERIAL_SOURCES
        serial/src/serial.cc
        serial/src/impl/unix.cc
        serial/src/impl/list_ports/list_ports_linux.cc
)
include_directories(
        ${OPENGL_INCLUDE_DIR}
        ${GLEW_INCLUDE_DIRS}
        imgui
        ${CMAKE_SOURCE_DIR}
)

file(GLOB IMGUI_SOURCES "imgui/*.cpp")

add_executable(SandSim main.cpp ${IMGUI_SOURCES} ${SERIAL_SOURCES})

target_link_libraries(SandSim
        OpenGL::GL
        GLEW::GLEW
        glfw
        rt
        pthread
)