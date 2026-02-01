#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <algorithm>
#include <cstdlib>

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

    void Update() {
        // Bottom-up iteration for falling sand
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
        // 1. Try down
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) {
            Move(x, y, x, y + 1);
        }
        // 2. Try diagonal
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

ImU32 GetColor(MaterialType type) {
    if (type == SAND) return IM_COL32(235, 200, 100, 255); // Gold
    return IM_COL32(0, 0, 0, 0); // Transparent
}

int main() {
    // Setup Window
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "SandSim", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    if (glewInit() != GLEW_OK) return 1;

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::StyleColorsLight();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    SandSimulation sim;
    float timeAccumulator = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        timeAccumulator += ImGui::GetIO().DeltaTime * 1000.0f;
        if (timeAccumulator >= sim.tickDelayMs) {
            sim.Update();
            timeAccumulator = 0.0f;
        }

        // Render Start
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // UI Panel
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls");
        ImGui::SliderFloat("Tick Delay (ms)", &sim.tickDelayMs, 1.0f, 200.0f);
        static int size[2] = { sim.width, sim.height };
        if (ImGui::SliderInt2("Size", size, 10, 200)) {
            sim.Resize(size[0], size[1]);
        }
        if (ImGui::Button("Reset")) sim.Clear();
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        // Simulation Canvas
        ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Simulation View");

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();

        float cellW = avail.x / sim.width;
        float cellH = avail.y / sim.height;
        float cellSize = (cellW < cellH) ? cellW : cellH;

        // Background
        draw_list->AddRectFilled(p, ImVec2(p.x + sim.width * cellSize, p.y + sim.height * cellSize), IM_COL32(255, 255, 255, 255));

        // Grid Lines
        for (int i = 0; i <= sim.width; ++i)
            draw_list->AddLine(ImVec2(p.x + i * cellSize, p.y), ImVec2(p.x + i * cellSize, p.y + sim.height * cellSize), IM_COL32(200, 200, 200, 255));
        for (int i = 0; i <= sim.height; ++i)
            draw_list->AddLine(ImVec2(p.x, p.y + i * cellSize), ImVec2(p.x + sim.width * cellSize, p.y + i * cellSize), IM_COL32(200, 200, 200, 255));

        // Cells
        for (int y = 0; y < sim.height; ++y) {
            for (int x = 0; x < sim.width; ++x) {
                if (sim.Get(x, y).type != EMPTY) {
                    ImVec2 min = ImVec2(p.x + x * cellSize, p.y + y * cellSize);
                    ImVec2 max = ImVec2(min.x + cellSize, min.y + cellSize);
                    draw_list->AddRectFilled(min, max, GetColor(sim.Get(x, y).type));
                }
            }
        }

        // Input
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered()) {
            ImVec2 m = ImGui::GetMousePos();
            int gx = (int)((m.x - p.x) / cellSize);
            int gy = (int)((m.y - p.y) / cellSize);
            sim.Set(gx, gy, SAND);
        }

        ImGui::End();

        // Render End
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