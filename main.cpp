#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cmath>

// Use GLM for vectors
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const int INITIAL_WIDTH = 60;
const int INITIAL_HEIGHT = 60;

enum MaterialType {
    EMPTY = 0,
    SAND,
    COUNT
};

struct Cell {
    MaterialType type;
};

class SandSimulation {
public:
    int width = INITIAL_WIDTH;
    int height = INITIAL_HEIGHT;
    std::vector<Cell> grid;
    float tickDelayMs = 16.0f;

    SandSimulation() {
        Resize(width, height);
    }

    void Resize(int w, int h) {
        width = w;
        height = h;
        grid.assign(width * height, { EMPTY });
    }

    void Clear() {
        std::fill(grid.begin(), grid.end(), Cell{ EMPTY });
    }

    Cell& Get(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return boundaryCell;
        return grid[y * width + x];
    }

    void Set(int x, int y, MaterialType type) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            grid[y * width + x].type = type;
        }
    }

    // Count sand pixels within a radius
    int CountSand(int cx, int cy, int radius) {
        int count = 0;
        int r2 = radius * radius;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                // Check bounds
                if (x < 0 || x >= width || y < 0 || y >= height) continue;

                // Check circle distance
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) {
                    if (Get(x, y).type == SAND) {
                        count++;
                    }
                }
            }
        }
        return count;
    }

    void Update() {
        for (int y = height - 1; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                if (Get(x, y).type == SAND) {
                    UpdateSand(x, y);
                }
            }
        }
    }

private:
    Cell boundaryCell = { EMPTY };

    void UpdateSand(int x, int y) {
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) {
            Move(x, y, x, y + 1);
        }
        else if (y + 1 < height) {
            bool leftEmpty = (x - 1 >= 0) && Get(x - 1, y + 1).type == EMPTY;
            bool rightEmpty = (x + 1 < width) && Get(x + 1, y + 1).type == EMPTY;

            if (leftEmpty && rightEmpty) {
                int offset = (rand() % 2 == 0) ? -1 : 1;
                Move(x, y, x + offset, y + 1);
            } else if (leftEmpty) {
                Move(x, y, x - 1, y + 1);
            } else if (rightEmpty) {
                Move(x, y, x + 1, y + 1);
            }
        }
    }

    void Move(int x1, int y1, int x2, int y2) {
        Cell temp = Get(x1, y1);
        Set(x1, y1, EMPTY);
        Set(x2, y2, temp.type);
    }
};

// --- Haptic System Implementation ---
class HapticSystem {
public:
    glm::vec2 devicePos = { 0.0f, 0.0f }; // Mouse / Handle position
    glm::vec2 proxyPos  = { 0.0f, 0.0f }; // Virtual "Proxy" position

    // Tuning parameters
    float radius = 4.0f;       // Radius of the tool
    float frictionCoef = 0.2f; // How "thick" the sand feels

    void Update(glm::vec2 targetDevicePos, SandSimulation& sim) {
        devicePos = targetDevicePos;

        // 1. Calculate Sand Density at the PROXY location
        int sandCount = sim.CountSand((int)proxyPos.x, (int)proxyPos.y, (int)radius);

        // 2. Calculate Viscosity (0.0 = Stuck, 1.0 = Free)
        float viscosity = 1.0f / (1.0f + (sandCount * frictionCoef));

        // 3. Move Proxy towards Device (damped by viscosity)
        glm::vec2 diff = devicePos - proxyPos;
        proxyPos += diff * viscosity;
    }

    void Render(ImDrawList* draw_list, ImVec2 origin, float cellSize) {
        ImVec2 screenDev = ImVec2(origin.x + devicePos.x * cellSize, origin.y + devicePos.y * cellSize);
        ImVec2 screenProx = ImVec2(origin.x + proxyPos.x * cellSize, origin.y + proxyPos.y * cellSize);

        // 1. Draw Proxy (Red Circle)
        draw_list->AddCircleFilled(screenProx, radius * cellSize, IM_COL32(255, 50, 50, 200));

        // 2. Draw Device (Green Outline)
        draw_list->AddCircle(screenDev, radius * cellSize, IM_COL32(50, 255, 50, 150), 0, 2.0f);

        // 3. Draw Spring Force (Blue Arrow)
        float dist = glm::distance(devicePos, proxyPos);
        if (dist > 0.1f) {
            ImVec2 arrowEnd = ImVec2(
                screenDev.x + (proxyPos.x - devicePos.x) * cellSize * 2.0f,
                screenDev.y + (proxyPos.y - devicePos.y) * cellSize * 2.0f
            );
            draw_list->AddLine(screenDev, arrowEnd, IM_COL32(50, 100, 255, 255), 3.0f);
        }
    }
};

ImU32 GetColor(MaterialType type) {
    if (type == SAND) return IM_COL32(235, 200, 100, 255);
    return IM_COL32(0, 0, 0, 0);
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "SandSim + Haptics", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (glewInit() != GLEW_OK) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsLight();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    SandSimulation sim;
    HapticSystem haptics;

    float timeAccumulator = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        timeAccumulator += ImGui::GetIO().DeltaTime * 1000.0f;
        if (timeAccumulator >= sim.tickDelayMs) {
            sim.Update();
            timeAccumulator = 0.0f;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // --- Controls Window ---
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls");
        ImGui::SliderFloat("Sim Speed (ms)", &sim.tickDelayMs, 1.0f, 200.0f);
        ImGui::Separator();

        // Haptic Settings
        ImGui::Text("Haptic Settings");
        ImGui::SliderFloat("Cursor Radius", &haptics.radius, 1.0f, 10.0f); // New Setting
        ImGui::SliderFloat("Friction", &haptics.frictionCoef, 0.01f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Device (Mouse) vs Proxy (Red)");

        static int size[2] = { sim.width, sim.height };
        if (ImGui::SliderInt2("Size", size, 10, 200)) {
            sim.Resize(size[0], size[1]);
        }
        if (ImGui::Button("Reset Sand")) sim.Clear();
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        // --- Simulation View ---
        ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Simulation View");

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();

        float cellW = avail.x / sim.width;
        float cellH = avail.y / sim.height;
        float cellSize = (cellW < cellH) ? cellW : cellH;

        // Draw Background
        draw_list->AddRectFilled(p, ImVec2(p.x + sim.width * cellSize, p.y + sim.height * cellSize), IM_COL32(255, 255, 255, 255));

        // Draw Grid Lines
        for (int i = 0; i <= sim.width; ++i)
            draw_list->AddLine(ImVec2(p.x + i * cellSize, p.y), ImVec2(p.x + i * cellSize, p.y + sim.height * cellSize), IM_COL32(220, 220, 220, 255));
        for (int i = 0; i <= sim.height; ++i)
            draw_list->AddLine(ImVec2(p.x, p.y + i * cellSize), ImVec2(p.x + sim.width * cellSize, p.y + i * cellSize), IM_COL32(220, 220, 220, 255));

        // Draw Sand Cells
        for (int y = 0; y < sim.height; ++y) {
            for (int x = 0; x < sim.width; ++x) {
                if (sim.Get(x, y).type != EMPTY) {
                    ImVec2 min = ImVec2(p.x + x * cellSize, p.y + y * cellSize);
                    ImVec2 max = ImVec2(min.x + cellSize, min.y + cellSize);
                    draw_list->AddRectFilled(min, max, GetColor(sim.Get(x, y).type));
                }
            }
        }

        // --- Haptic Logic Update ---
        if (ImGui::IsWindowHovered()) {
            ImVec2 m = ImGui::GetMousePos();

            glm::vec2 mouseGridPos;
            mouseGridPos.x = (m.x - p.x) / cellSize;
            mouseGridPos.y = (m.y - p.y) / cellSize;

            haptics.Update(mouseGridPos, sim);

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
               sim.Set((int)mouseGridPos.x, (int)mouseGridPos.y, SAND);
            }
        }

        haptics.Render(draw_list, p, cellSize);

        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}