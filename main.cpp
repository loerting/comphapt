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
    WETSAND,
    WATER,
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

    SandSimulation() { Resize(width, height); }

    void Resize(int w, int h) {
        width = w;
        height = h;
        grid.assign(width * height, { EMPTY });
    }

    void Clear() { std::fill(grid.begin(), grid.end(), Cell{ EMPTY }); }

    // Boundary-safe getter
    Cell Get(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return boundaryCell;
        return grid[y * width + x];
    }

    // Boundary-safe setter
    void Set(int x, int y, MaterialType type) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            grid[y * width + x].type = type;
        }
    }

    // Public Move function for the Haptic System
    bool Move(int x1, int y1, int x2, int y2) {
        // Source must be valid, Target must be valid bounds
        if (x1 < 0 || x1 >= width || y1 < 0 || y1 >= height) return false;
        if (x2 < 0 || x2 >= width || y2 < 0 || y2 >= height) return false;

        // Check if target is actually empty (don't overwrite sand)
        if (grid[y2 * width + x2].type != EMPTY) return false;

        Cell temp = grid[y1 * width + x1];
        grid[y1 * width + x1].type = EMPTY;
        grid[y2 * width + x2].type = temp.type;
        return true;
    }

    float GetResistance(int cx, int cy, int radius) {
        float totalResistance = 0.0f;
        int r2 = radius * radius;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (x < 0 || x >= width || y < 0 || y >= height) continue;
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r2) {
                    MaterialType type = Get(x, y).type;

                    if (type == SAND)         totalResistance += 0.1f;
                    else if (type == WETSAND) totalResistance += 0.5f;
                    else if (type == WATER)   totalResistance += 0.02f;
                }
            }
        }
        return totalResistance;
    }

    // BFS/Spiral search to find nearest empty cell to (targetX, targetY)
    // Returns glm::ivec2(-1, -1) if nothing found
    glm::ivec2 FindNearestEmpty(int targetX, int targetY, int maxRadius) {
        // 1. Check the target itself first
        if (Get(targetX, targetY).type == EMPTY &&
            targetX >= 0 && targetX < width && targetY >= 0 && targetY < height) {
            return glm::ivec2(targetX, targetY);
        }

        // 2. Spiral/Square search outwards
        for (int r = 1; r <= maxRadius; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    // Only check the outer ring (optimization)
                    if (abs(dx) != r && abs(dy) != r) continue;

                    int nx = targetX + dx;
                    int ny = targetY + dy;

                    // Bounds check
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        if (Get(nx, ny).type == EMPTY) {
                            return glm::ivec2(nx, ny);
                        }
                    }
                }
            }
        }
        return glm::ivec2(-1, -1);
    }

    void Update() {
        for (int y = height - 1; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                MaterialType type = grid[y * width + x].type;
                if (type == SAND) UpdateSand(x, y);
                else if (type == WETSAND) UpdateWetSand(x, y);
                else if (type == WATER) UpdateWater(x, y);
            }
        }
    }

private:
    Cell boundaryCell = { SAND }; // Treat boundary as solid/sand so we don't push into it

    void UpdateSand(int x, int y) {

        if (y + 1 < height && Get(x, y + 1).type == WATER) {
            Swap(x, y, x, y + 1);
            return;
        }
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

    void UpdateWetSand(int x, int y) {
        // Sink through water
        if (y + 1 < height && Get(x, y + 1).type == WATER) {
            Swap(x, y, x, y + 1);
            return;
        }
        // Normal falling
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) {
            Move(x, y, x, y + 1);
            return;
        }
    }

    void UpdateWater(int x, int y) {

        if (TryWetSand(x, y)) {
            return; // water is gone
        }

        // 1. Move Down
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) {
            Move(x, y, x, y + 1);
        }
            // 2. Move Diagonally Down
        else if (y + 1 < height) {
            bool leftEmpty = (x - 1 >= 0) && Get(x - 1, y + 1).type == EMPTY;
            bool rightEmpty = (x + 1 < width) && Get(x + 1, y + 1).type == EMPTY;

            if (leftEmpty && rightEmpty) {
                Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y + 1);
            } else if (leftEmpty) {
                Move(x, y, x - 1, y + 1);
            } else if (rightEmpty) {
                Move(x, y, x + 1, y + 1);
            }
                // 3. Move Horizontally (The "Fluid" part)
            else {
                bool leftSide = (x - 1 >= 0) && Get(x - 1, y).type == EMPTY;
                bool rightSide = (x + 1 < width) && Get(x + 1, y).type == EMPTY;

                if (leftSide && rightSide) {
                    Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y);
                } else if (leftSide) {
                    Move(x, y, x - 1, y);
                } else if (rightSide) {
                    Move(x, y, x + 1, y);
                }
            }
        }
    }

    bool TryWetSand(int wx, int wy) {
        static const int offsets[8][2] = {
                { 0,  1}, { 0, -1}, { 1,  0}, {-1,  0},
                { 1,  1}, {-1,  1}, { 1, -1}, {-1, -1}
        };

        for (auto& o : offsets) {
            int sx = wx + o[0];
            int sy = wy + o[1];

            if (sx < 0 || sx >= width || sy < 0 || sy >= height)
                continue;

            if (Get(sx, sy).type == SAND) {
                // Convert sand → wet sand
                Set(sx, sy, WETSAND);

                // Consume this water
                Set(wx, wy, EMPTY);

                return true; // reaction happened
            }
        }

        return false;
    }

    bool Swap(int x1, int y1, int x2, int y2) {
        if (x1 < 0 || x1 >= width || y1 < 0 || y1 >= height) return false;
        if (x2 < 0 || x2 >= width || y2 < 0 || y2 >= height) return false;

        std::swap(grid[y1 * width + x1], grid[y2 * width + x2]);
        return true;
    }

};

// --- Haptic System Implementation ---
class HapticSystem {
public:
    glm::vec2 devicePos = { 0.0f, 0.0f };
    glm::vec2 proxyPos  = { 0.0f, 0.0f };
    float radius = 4.0f;
    float frictionCoef = 0.2f;

    void Update(glm::vec2 targetDevicePos, SandSimulation& sim) {
        devicePos = targetDevicePos;
        float resistance = sim.GetResistance((int)proxyPos.x, (int)proxyPos.y, (int)radius);
        float viscosity = 1.0f / (1.0f + (resistance * frictionCoef));
        glm::vec2 diff = devicePos - proxyPos;
        proxyPos += diff * viscosity;

        // Perform displacement AFTER moving the proxy
        DisplaceSand(sim);
    }

    void DisplaceSand(SandSimulation& sim) {
        int r = (int)std::ceil(radius);
        int px = (int)proxyPos.x;
        int py = (int)proxyPos.y;
        float rSq = radius * radius;

        // Iterate over the bounding box of the cursor
        for (int y = py - r; y <= py + r; ++y) {
            for (int x = px - r; x <= px + r; ++x) {
                // 1. Check if pixel is SAND and inside circle
                if (sim.Get(x, y).type != EMPTY) {
                    float dx = (float)x - proxyPos.x;
                    float dy = (float)y - proxyPos.y;
                    float distSq = dx*dx + dy*dy;

                    if (distSq <= rSq) {
                        // 2. Calculate Push Vector
                        glm::vec2 dir(dx, dy);

                        // Handle center case (push up if exactly centered)
                        if (glm::length(dir) < 0.01f) dir = glm::vec2(0, -1);
                        else dir = glm::normalize(dir);

                        // 3. Calculate Target Position (Just outside radius)
                        glm::vec2 targetPos = proxyPos + dir * (radius + 1.5f); // +1.5 ensures we clear the int boundary
                        int tx = (int)targetPos.x;
                        int ty = (int)targetPos.y;

                        // 4. Try to find an empty spot there or nearby
                        // Search radius 3 is usually enough for "fluid" motion
                        glm::ivec2 bestSpot = sim.FindNearestEmpty(tx, ty, 3);

                        // 5. Move if valid spot found
                        if (bestSpot.x != -1) {
                            sim.Move(x, y, bestSpot.x, bestSpot.y);
                        }
                    }
                }
            }
        }
    }

    void Render(ImDrawList* draw_list, ImVec2 origin, float cellSize) {
        ImVec2 screenDev = ImVec2(origin.x + devicePos.x * cellSize, origin.y + devicePos.y * cellSize);
        ImVec2 screenProx = ImVec2(origin.x + proxyPos.x * cellSize, origin.y + proxyPos.y * cellSize);

        draw_list->AddCircleFilled(screenProx, radius * cellSize, IM_COL32(255, 50, 50, 200));
        draw_list->AddCircle(screenDev, radius * cellSize, IM_COL32(50, 255, 50, 150), 0, 2.0f);

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
    if (type == WETSAND) return IM_COL32(160, 130, 70, 255);
    if (type == WATER) return IM_COL32(0, 120, 255, 200);
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
    static int currentMaterial = SAND;

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
        ImGui::RadioButton("Dry Sand", &currentMaterial, SAND);
        ImGui::RadioButton("Wet Sand", &currentMaterial, WETSAND);
        ImGui::RadioButton("Water", &currentMaterial, WATER);
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
               sim.Set((int)mouseGridPos.x, (int)mouseGridPos.y, (MaterialType) currentMaterial);
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