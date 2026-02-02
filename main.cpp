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
#include <string>
#include <sstream>
#include <iomanip>

// --- Serial Library ---
#include <serial/serial.h>

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

// --- Haptic Device Communication Class ---
class HapticDevice {
private:
    serial::Serial* mySerial = nullptr;
    float currentPositionMeters = 0.0f;

public:
    std::string port = "/dev/ttyACM0";
    unsigned long baud = 115200;
    bool connected = false;

    ~HapticDevice() { Disconnect(); }

    bool Connect() {
        try {
            // Timeout(0) = Non-blocking
            mySerial = new serial::Serial(port, baud, serial::Timeout::simpleTimeout(0));

            if (mySerial->isOpen()) {
                connected = true;
                currentPositionMeters = 0.0f;
                mySerial->flushInput();
                return true;
            }
            return false;
        } catch (std::exception &e) {
            std::cout << "[Error] Connect: " << e.what() << std::endl;
            return false;
        }
    }

    void Disconnect() {
        if (mySerial) {
            if (mySerial->isOpen()) mySerial->write("F 0.0\n");
            mySerial->close();
            delete mySerial;
            mySerial = nullptr;
        }
        connected = false;
    }

    void Sync(float forceOutputNewtons) {
        if (!connected || !mySerial) return;

        int maxReads = 50;
        while (mySerial->available() && maxReads-- > 0) {
            std::string line = mySerial->readline();
            if (line.length() > 4 && line.back() == '\n') {
                if (line[0] == 'P') {
                    try {
                        float val = std::stof(line.substr(2));
                        currentPositionMeters = val;
                    } catch (...) {}
                }
            }
        }

        static float lastSentForce = -999.0f;
        static double lastSendTime = 0.0;
        double currentTime = glfwGetTime();

        if (abs(forceOutputNewtons - lastSentForce) > 0.005f || (currentTime - lastSendTime) > 0.05) {
            std::stringstream ss;
            // Force decimal format
            ss << std::fixed << std::setprecision(5) << "F " << forceOutputNewtons << "\n";
            try {
                mySerial->write(ss.str());
                lastSentForce = forceOutputNewtons;
                lastSendTime = currentTime;
            } catch (...) {}
        }
    }

    float GetPositionMeters() const {
        return currentPositionMeters;
    }
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

    // Precise float-based resistance check (Kept this improvement)
    float GetResistance(float cx, float cy, float radius) {
        float totalResistance = 0.0f;
        float r2 = radius * radius;

        int minX = (int)floor(cx - radius);
        int maxX = (int)ceil(cx + radius);
        int minY = (int)floor(cy - radius);
        int maxY = (int)ceil(cy + radius);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (x < 0 || x >= width || y < 0 || y >= height) continue;

                float dx = x - cx;
                float dy = y - cy;

                if (dx*dx + dy*dy <= r2) {
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

    glm::vec2 proxyPos  = { 30.0f, 30.0f };
    glm::vec2 devicePos = { 30.0f, 30.0f };

    // Smooth Resistance State
    float smoothedResistance = 0.0f;

    glm::vec2 anchorPos = { 30.0f, 30.0f };
    AxisMode  currentAxis = X_AXIS;
    ControlMode currentMode = MODE_1DOF;

    float rawInputVal = 0.0f;

    // Settings
    float radius = 4.0f;
    float frictionCoef = 5.0f;
    float hapkitScale = 500.0f;
    float springK = 0.5f;

    // Output
    float currentForce1D = 0.0f;

    void Recenter(glm::vec2 newCenter) {
        anchorPos = newCenter;
        proxyPos = newCenter;
        devicePos = newCenter;
        rawInputVal = 0.0f;
    }

    void Update(glm::vec2 mousePos, float rawInputMeters, bool isMouseInput, SandSimulation& sim) {

        if (currentMode == MODE_2DOF) {
            devicePos = mousePos;
            currentForce1D = 0.0f;
        }
        else {
            if (isMouseInput) {
                if (currentAxis == X_AXIS) rawInputVal = (mousePos.x - anchorPos.x) / hapkitScale;
                else                       rawInputVal = (mousePos.y - anchorPos.y) / hapkitScale;
            } else {
                rawInputVal = rawInputMeters;
            }

            if (rawInputVal > 0.08f) rawInputVal = 0.08f;
            if (rawInputVal < -0.08f) rawInputVal = -0.08f;

            if (currentAxis == X_AXIS) devicePos = glm::vec2(anchorPos.x + rawInputVal * hapkitScale, anchorPos.y);
            else                       devicePos = glm::vec2(anchorPos.x, anchorPos.y + rawInputVal * hapkitScale);
        }

        // --- Low Pass Filter on Resistance (Kept this improvement) ---
        float rawResistance = sim.GetResistance(proxyPos.x, proxyPos.y, radius);
        float alpha = 0.2f;
        smoothedResistance = smoothedResistance * (1.0f - alpha) + rawResistance * alpha;

        float viscosity = 1.0f / (1.0f + (smoothedResistance * frictionCoef));

        // Move Proxy
        glm::vec2 diff = devicePos - proxyPos;
        proxyPos += diff * viscosity;

        DisplaceSand(sim);

        // --- Force Calculation (Standard Spring) ---
        // INVERTED spring force for haptics
        glm::vec2 forceVec = (proxyPos - devicePos) * -springK;

        if (glm::length(forceVec) < 0.025f) forceVec = glm::vec2(0.0f);

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
                        glm::ivec2 best = sim.FindNearestEmpty((int)target.x, (int)target.y, 3);
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

        draw_list->AddCircleFilled(sProx, radius * cellSize, IM_COL32(255, 50, 50, 200));
        draw_list->AddCircle(sDev, radius * cellSize, IM_COL32(50, 255, 50, 200), 0, 2.0f);
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
    HapticDevice device;

    static int currentMaterial = SAND;
    char portBuffer[64] = "/dev/ttyACM0";

    float timeAccumulator = 0.0f;
    bool simulateInput = true;

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

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls");
        ImGui::SliderFloat("Sim Speed (ms)", &sim.tickDelayMs, 1.0f, 200.0f);
        ImGui::RadioButton("Dry", &currentMaterial, SAND); ImGui::SameLine();
        ImGui::RadioButton("Wet", &currentMaterial, WETSAND); ImGui::SameLine();
        ImGui::RadioButton("H2O", &currentMaterial, WATER);

        ImGui::Separator();

        ImGui::Text("Haptic Device");
        ImGui::InputText("Port", portBuffer, 64);
        if (ImGui::Button(device.connected ? "Disconnect" : "Connect")) {
            if (device.connected) {
                device.Disconnect();
                simulateInput = true;
            } else {
                device.port = std::string(portBuffer);
                if (device.Connect()) {
                    simulateInput = false;
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(device.connected ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1),
            device.connected ? "Connected" : "Disconnected");

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

            ImGui::SliderFloat("Scale (Pix/m)", &haptics.hapkitScale, 100.0f, 2000.0f);
            ImGui::Text("Input (m): %.4f", haptics.rawInputVal);
            ImGui::Text("Output (N): %.2f", haptics.currentForce1D);
        }

        ImGui::Separator();
        ImGui::SliderFloat("Stiffness (k)", &haptics.springK, 0.001f, 5.0f);
        ImGui::SliderFloat("Radius", &haptics.radius, 1.0f, 10.0f);
        ImGui::SliderFloat("Friction", &haptics.frictionCoef, 0.01f, 10.0f);
        ImGui::Text("Smooth Res: %.2f", haptics.smoothedResistance);

        ImGui::Separator();
        ImGui::Text("Press 'G' to Re-Center Anchor");
        ImGui::Checkbox("Drive w/ Mouse", &simulateInput);

        if (ImGui::Button("Reset Sand")) sim.Clear();
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Simulation View");

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float cellW = avail.x / sim.width;
        float cellH = avail.y / sim.height;
        float cellSize = (cellW < cellH) ? cellW : cellH;

        draw_list->AddRectFilled(p, ImVec2(p.x + sim.width * cellSize, p.y + sim.height * cellSize), IM_COL32(255, 255, 255, 255));
        for (int i = 0; i <= sim.width; ++i)
            draw_list->AddLine(ImVec2(p.x + i * cellSize, p.y), ImVec2(p.x + i * cellSize, p.y + sim.height * cellSize), IM_COL32(220, 220, 220, 255));
        for (int i = 0; i <= sim.height; ++i)
            draw_list->AddLine(ImVec2(p.x, p.y + i * cellSize), ImVec2(p.x + sim.width * cellSize, p.y + i * cellSize), IM_COL32(220, 220, 220, 255));

        for (int y = 0; y < sim.height; ++y) {
            for (int x = 0; x < sim.width; ++x) {
                if (sim.Get(x, y).type != EMPTY) {
                    ImVec2 min = ImVec2(p.x + x * cellSize, p.y + y * cellSize);
                    ImVec2 max = ImVec2(min.x + cellSize, min.y + cellSize);
                    draw_list->AddRectFilled(min, max, GetColor(sim.Get(x, y)));
                }
            }
        }

        if (ImGui::IsWindowHovered()) {
            ImVec2 m = ImGui::GetMousePos();
            glm::vec2 mouseGridPos;
            mouseGridPos.x = (m.x - p.x) / cellSize;
            mouseGridPos.y = (m.y - p.y) / cellSize;

            if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                haptics.Recenter(mouseGridPos);
            }

            bool userIsDrawing = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (userIsDrawing) {
                int initialSoak = (currentMaterial == WETSAND) ? soakThreshold : 0;
                sim.Set((int)mouseGridPos.x, (int)mouseGridPos.y, (MaterialType)currentMaterial, initialSoak);
            }

            if (device.connected) {
                device.Sync(haptics.currentForce1D);
            }

            if (simulateInput) {
                haptics.Update(mouseGridPos, 0.0f, true, sim);
            } else {
                float meters = device.GetPositionMeters();
                haptics.Update(glm::vec2(0,0), meters, false, sim);
            }
        } else {
             if (device.connected) device.Sync(haptics.currentForce1D);

             if (!simulateInput && device.connected) {
                 haptics.Update(glm::vec2(0,0), device.GetPositionMeters(), false, sim);
             } else {
                 haptics.Update(haptics.devicePos, 0.0f, false, sim);
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