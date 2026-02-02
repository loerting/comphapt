#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

const int INITIAL_WIDTH = 60;
const int INITIAL_HEIGHT = 60;
const int soakThreshold = 2;

enum MaterialType {
    EMPTY = 0,
    SAND,
    WETSAND,
    WATER,
    COUNT
};

struct Cell {
    MaterialType type;
    int soak = 0;
};

// --- Sand Simulation ---
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

    Cell Get(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return boundaryCell;
        return grid[y * width + x];
    }

    void Set(int x, int y, MaterialType type, int soak = 0) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            grid[y * width + x].type = type;
            grid[y * width + x].soak = soak;
        }
    }

    bool Move(int x1, int y1, int x2, int y2) {
        if (x1 < 0 || x1 >= width || y1 < 0 || y1 >= height) return false;
        if (x2 < 0 || x2 >= width || y2 < 0 || y2 >= height) return false;
        if (grid[y2 * width + x2].type != EMPTY) return false;

        Cell temp = grid[y1 * width + x1];
        grid[y1 * width + x1] = {EMPTY, 0};
        grid[y2 * width + x2] = temp;
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
                    else if (type == WETSAND) totalResistance += Get(x, y).soak * 0.02f + 0.1f;
                    else if (type == WATER)   totalResistance += 0.02f;
                }
            }
        }
        return totalResistance;
    }

    glm::ivec2 FindNearestEmpty(int targetX, int targetY, int maxRadius) {
        if (Get(targetX, targetY).type == EMPTY &&
            targetX >= 0 && targetX < width && targetY >= 0 && targetY < height) {
            return glm::ivec2(targetX, targetY);
        }
        for (int r = 1; r <= maxRadius; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (abs(dx) != r && abs(dy) != r) continue;
                    int nx = targetX + dx;
                    int ny = targetY + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        if (Get(nx, ny).type == EMPTY) return glm::ivec2(nx, ny);
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
    Cell boundaryCell = { SAND };

    void UpdateSand(int x, int y) {
        if (y + 1 < height && Get(x, y + 1).type == WATER) { Swap(x, y, x, y + 1); return; }
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) { Move(x, y, x, y + 1); }
        else if (y + 1 < height) {
            bool leftEmpty = (x - 1 >= 0) && Get(x - 1, y + 1).type == EMPTY;
            bool rightEmpty = (x + 1 < width) && Get(x + 1, y + 1).type == EMPTY;
            if (leftEmpty && rightEmpty) {
                int offset = (rand() % 2 == 0) ? -1 : 1;
                Move(x, y, x + offset, y + 1);
            } else if (leftEmpty) Move(x, y, x - 1, y + 1);
            else if (rightEmpty) Move(x, y, x + 1, y + 1);
        }
    }

    void UpdateWetSand(int x, int y) {
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) { Move(x, y, x, y + 1); return; }
        if (y + 1 < height && Get(x, y + 1).type == WATER) { Swap(x, y, x, y + 1); return; }
    }

    void UpdateWater(int x, int y) {
        if (TryWetSand(x, y)) return;
        if (y + 1 < height && Get(x, y + 1).type == EMPTY) { Move(x, y, x, y + 1); }
        else if (y + 1 < height) {
            bool left = (x - 1 >= 0) && Get(x - 1, y + 1).type == EMPTY;
            bool right = (x + 1 < width) && Get(x + 1, y + 1).type == EMPTY;
            if (left && right) Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y + 1);
            else if (left) Move(x, y, x - 1, y + 1);
            else if (right) Move(x, y, x + 1, y + 1);
            else {
                bool lSide = (x - 1 >= 0) && Get(x - 1, y).type == EMPTY;
                bool rSide = (x + 1 < width) && Get(x + 1, y).type == EMPTY;
                if (lSide && rSide) Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y);
                else if (lSide) Move(x, y, x - 1, y);
                else if (rSide) Move(x, y, x + 1, y);
            }
        }
    }

    bool TryWetSand(int wx, int wy) {
        static const int offsets[9][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {0, 2}};
        for (auto& o : offsets) {
            int sx = wx + o[0];
            int sy = wy + o[1];
            if (sx < 0 || sx >= width || sy < 0 || sy >= height) continue;
            Cell& cell = grid[sy * width + sx];
            if (cell.type == SAND) { Set(sx, sy, WETSAND, 1); Set(wx, wy, EMPTY, 0); return true; }
            if (cell.type == WETSAND && cell.soak < soakThreshold) { cell.soak++; Set(wx, wy, EMPTY, 0); return true; }
            if (cell.type == WETSAND && cell.soak >= soakThreshold && sy < wy) { Swap(wx, wy, sx, sy); return true; }
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
    enum AxisMode { X_AXIS, Y_AXIS };
    enum ControlMode { MODE_1DOF, MODE_2DOF };

    // -- State --
    glm::vec2 proxyPos  = { 30.0f, 30.0f };
    glm::vec2 devicePos = { 30.0f, 30.0f };

    // -- Configuration --
    glm::vec2 anchorPos = { 30.0f, 30.0f };
    AxisMode  currentAxis = X_AXIS;
    ControlMode currentMode = MODE_1DOF;

    float     rawHapkitPos = 0.0f;          // 1D Input (-40 to +40)

    // -- Settings --
    float radius = 4.0f;
    float frictionCoef = 1.0f;
    float hapkitScale = 1.0f;
    float springK = 0.5f;

    // -- Output --
    float currentForce1D = 0.0f;

    void Recenter(glm::vec2 newCenter) {
        anchorPos = newCenter;
        proxyPos = newCenter;
        devicePos = newCenter;
        rawHapkitPos = 0.0f;
    }

    // Unified Update
    // inputTarget: The position of the "Input Device" (Mouse) in simulation space
    void Update(glm::vec2 inputTarget, SandSimulation& sim) {

        if (currentMode == MODE_2DOF) {
            // --- 2D MODE (Mouse/Free) ---
            devicePos = inputTarget;
            // Clear 1D variables for safety
            rawHapkitPos = 0.0f;
            currentForce1D = 0.0f;
        }
        else {
            // --- 1D MODE (Rail/Hapkit) ---
            // Calculate scalar input by projecting mouse onto the axis relative to anchor
            float inputVal = 0.0f;
            if (currentAxis == X_AXIS) {
                inputVal = (inputTarget.x - anchorPos.x) / hapkitScale;
            } else {
                inputVal = (inputTarget.y - anchorPos.y) / hapkitScale;
            }

            // Clamp to Hapkit physical range
            if (inputVal > 40.0f) inputVal = 40.0f;
            if (inputVal < -40.0f) inputVal = -40.0f;

            rawHapkitPos = inputVal;

            // Constrain Device to Rail
            if (currentAxis == X_AXIS) {
                devicePos = glm::vec2(anchorPos.x + rawHapkitPos * hapkitScale, anchorPos.y);
            } else {
                devicePos = glm::vec2(anchorPos.x, anchorPos.y + rawHapkitPos * hapkitScale);
            }
        }

        // --- Common Physics (Proxy follows Device) ---
        float resistance = sim.GetResistance((int)proxyPos.x, (int)proxyPos.y, (int)radius);
        float viscosity = 1.0f / (1.0f + (resistance * frictionCoef));

        glm::vec2 diff = devicePos - proxyPos;
        proxyPos += diff * viscosity;

        // Displacement
        DisplaceSand(sim);

        // --- Force Calculation ---
        glm::vec2 forceVec = (proxyPos - devicePos) * springK;

        // In 1D mode, we only output the scalar component
        if (currentMode == MODE_1DOF) {
            if (currentAxis == X_AXIS) currentForce1D = forceVec.x;
            else                       currentForce1D = forceVec.y;
        }
    }

    void DisplaceSand(SandSimulation& sim) {
        int r = (int)std::ceil(radius);
        int px = (int)proxyPos.x;
        int py = (int)proxyPos.y;
        float rSq = radius * radius;

        for (int y = py - r; y <= py + r; ++y) {
            for (int x = px - r; x <= px + r; ++x) {
                if (sim.Get(x, y).type != EMPTY) {
                    float dx = (float)x - proxyPos.x;
                    float dy = (float)y - proxyPos.y;
                    if (dx*dx + dy*dy <= rSq) {
                        glm::vec2 dir(dx, dy);
                        if (glm::length(dir) < 0.01f) dir = glm::vec2(0, -1);
                        else dir = glm::normalize(dir);

                        glm::vec2 target = proxyPos + dir * (radius + 1.5f);
                        glm::ivec2 best = sim.FindNearestEmpty((int)target.x, (int)target.y, 10);
                        if (best.x != -1) sim.Move(x, y, best.x, best.y);
                    }
                }
            }
        }
    }

    void Render(ImDrawList* draw_list, ImVec2 origin, float cellSize) {
        ImVec2 sDev = ImVec2(origin.x + devicePos.x * cellSize, origin.y + devicePos.y * cellSize);
        ImVec2 sProx = ImVec2(origin.x + proxyPos.x * cellSize, origin.y + proxyPos.y * cellSize);
        ImVec2 sAnch = ImVec2(origin.x + anchorPos.x * cellSize, origin.y + anchorPos.y * cellSize);

        // 1. Draw Rail/Anchor (Only in 1D Mode)
        if (currentMode == MODE_1DOF) {
            ImU32 railColor = IM_COL32(100, 100, 100, 100);
            float railLen = 2000.0f;
            if (currentAxis == X_AXIS) {
                draw_list->AddLine(ImVec2(sAnch.x - railLen, sAnch.y), ImVec2(sAnch.x + railLen, sAnch.y), railColor, 1.0f);
            } else {
                draw_list->AddLine(ImVec2(sAnch.x, sAnch.y - railLen), ImVec2(sAnch.x, sAnch.y + railLen), railColor, 1.0f);
            }
            draw_list->AddCircleFilled(sAnch, 4.0f, IM_COL32(255, 255, 0, 200));
        }

        // 2. Draw Proxy (Red)
        draw_list->AddCircleFilled(sProx, radius * cellSize, IM_COL32(255, 50, 50, 200));

        // 3. Draw Device (Green Outline)
        draw_list->AddCircle(sDev, radius * cellSize, IM_COL32(50, 255, 50, 200), 0, 2.0f);

        // 4. Draw Force Spring
        draw_list->AddLine(sDev, sProx, IM_COL32(50, 100, 255, 255), 2.0f);
    }
};

ImU32 GetColor(Cell cell) {
    MaterialType type = cell.type;
    if (type == SAND) return IM_COL32(235, 200, 100, 255);
    if (type == WETSAND) {
        if (cell.soak >= soakThreshold) return IM_COL32(100, 80, 40, 255);
        return IM_COL32(160, 130, 70, 255);
    }
    if (type == WATER) return IM_COL32(0, 120, 255, 200);
    return IM_COL32(0, 0, 0, 0);
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "SandSim Haptics", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

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
    bool simulateInput = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Logic ---
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
        ImGui::RadioButton("Dry", &currentMaterial, SAND); ImGui::SameLine();
        ImGui::RadioButton("Wet", &currentMaterial, WETSAND); ImGui::SameLine();
        ImGui::RadioButton("H2O", &currentMaterial, WATER);

        ImGui::Separator();
        ImGui::Text("Control Mode");
        int mode = (int)haptics.currentMode;
        if (ImGui::RadioButton("1D (Hapkit/Rail)", mode == 0)) haptics.currentMode = HapticSystem::MODE_1DOF;
        ImGui::SameLine();
        if (ImGui::RadioButton("2D (Mouse/Free)", mode == 1)) haptics.currentMode = HapticSystem::MODE_2DOF;

        if (haptics.currentMode == HapticSystem::MODE_1DOF) {
            ImGui::Text("Rail Axis:");
            int axis = (int)haptics.currentAxis;
            if (ImGui::RadioButton("X-Axis", axis == 0)) haptics.currentAxis = HapticSystem::X_AXIS;
            ImGui::SameLine();
            if (ImGui::RadioButton("Y-Axis", axis == 1)) haptics.currentAxis = HapticSystem::Y_AXIS;

            ImGui::SliderFloat("Scale", &haptics.hapkitScale, 0.1f, 2.0f);
            ImGui::Text("Output Force: %.2f", haptics.currentForce1D);
        }

        ImGui::Separator();
        ImGui::SliderFloat("Stiffness", &haptics.springK, 0.1f, 2.0f);
        ImGui::SliderFloat("Radius", &haptics.radius, 1.0f, 10.0f);
        ImGui::SliderFloat("Friction", &haptics.frictionCoef, 0.01f, 5.0f);

        ImGui::Separator();
        ImGui::Text("Press 'G' to Re-Center Anchor");
        ImGui::Checkbox("Drive w/ Mouse", &simulateInput);

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

        // Background & Grid
        draw_list->AddRectFilled(p, ImVec2(p.x + sim.width * cellSize, p.y + sim.height * cellSize), IM_COL32(255, 255, 255, 255));
        for (int i = 0; i <= sim.width; ++i)
            draw_list->AddLine(ImVec2(p.x + i * cellSize, p.y), ImVec2(p.x + i * cellSize, p.y + sim.height * cellSize), IM_COL32(220, 220, 220, 255));
        for (int i = 0; i <= sim.height; ++i)
            draw_list->AddLine(ImVec2(p.x, p.y + i * cellSize), ImVec2(p.x + sim.width * cellSize, p.y + i * cellSize), IM_COL32(220, 220, 220, 255));

        // Draw Sand
        for (int y = 0; y < sim.height; ++y) {
            for (int x = 0; x < sim.width; ++x) {
                if (sim.Get(x, y).type != EMPTY) {
                    ImVec2 min = ImVec2(p.x + x * cellSize, p.y + y * cellSize);
                    ImVec2 max = ImVec2(min.x + cellSize, min.y + cellSize);
                    draw_list->AddRectFilled(min, max, GetColor(sim.Get(x, y)));
                }
            }
        }

        // --- Haptic / Input Logic ---
        if (ImGui::IsWindowHovered()) {
            ImVec2 m = ImGui::GetMousePos();
            glm::vec2 mouseGridPos;
            mouseGridPos.x = (m.x - p.x) / cellSize;
            mouseGridPos.y = (m.y - p.y) / cellSize;

            // HANDLE KEY 'G' (Set Anchor)
            if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                haptics.Recenter(mouseGridPos);
            }

            // DRAW SAND (Left or Right Click)
            bool userIsDrawing = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (userIsDrawing) {
                int initialSoak = (currentMaterial == WETSAND) ? soakThreshold : 0;
                sim.Set((int)mouseGridPos.x, (int)mouseGridPos.y, (MaterialType)currentMaterial, initialSoak);
            }

            // SIMULATE HAPTICS
            // If "Drive w/ Mouse" is on, we pass the mouse pos as the "Input Device"
            // The HapticSystem decides if that means "2D Position" or "1D Projection"
            if (simulateInput) {
                haptics.Update(mouseGridPos, sim);
            } else {
                // If not simulating input (e.g. using Real Device), we'd pass raw data here.
                // For now, we just hold the last known position relative to anchor.
                // To keep physics alive, we call Update with the CURRENT device pos.
                haptics.Update(haptics.devicePos, sim);
            }
        } else {
             // Keep physics running
             haptics.Update(haptics.devicePos, sim);
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