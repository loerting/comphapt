#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Standard Library
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Serial Library
#include <serial/serial.h>

// GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// --- Constants ---
constexpr int INITIAL_WIDTH = 60;
constexpr int INITIAL_HEIGHT = 60;
constexpr int SOAK_THRESHOLD = 2;
constexpr float TICK_DELAY_DEFAULT = 16.0f;

// --- Types ---
enum class MaterialType {
    Empty = 0,
    Sand,
    WetSand,
    Water,
    Count
};

struct Cell {
    MaterialType type = MaterialType::Empty;
    int soak = 0;
};

// --- Haptic Device Communication Class ---
class HapticDevice {
private:
    std::unique_ptr<serial::Serial> m_serial;
    float m_currentPositionMeters = 0.0f;
    float m_lastSentForce = -999.0f;
    double m_lastSendTime = 0.0;

public:
    std::string port = "/dev/ttyUSB0";
    unsigned long baud = 115200;
    bool connected = false;

    HapticDevice() = default;
    ~HapticDevice() { Disconnect(); }

    // Disable copying
    HapticDevice(const HapticDevice&) = delete;
    HapticDevice& operator=(const HapticDevice&) = delete;

    bool Connect() {
        try {
            // Timeout(0) = Non-blocking
            m_serial = std::make_unique<serial::Serial>(port, baud, serial::Timeout::simpleTimeout(0));

            if (m_serial->isOpen()) {
                connected = true;
                m_currentPositionMeters = 0.0f;
                m_serial->flushInput();
                return true;
            }
            return false;
        } catch (const std::exception& e) {
            std::cerr << "[Error] Connect: " << e.what() << std::endl;
            return false;
        }
    }

    void Disconnect() {
        if (m_serial && m_serial->isOpen()) {
            try {
                m_serial->write("F 0.0\n");
                m_serial->close();
            } catch (...) {}
        }
        m_serial.reset();
        connected = false;
    }

    void Sync(float forceOutputNewtons) {
        if (!connected || !m_serial) return;

        // Read available data
        int maxReads = 50;
        try {
            while (m_serial->available() && maxReads-- > 0) {
                std::string line = m_serial->readline();
                if (line.length() > 4 && line.back() == '\n') {
                    if (line[0] == 'P') {
                        try {
                            m_currentPositionMeters = std::stof(line.substr(2));
                        } catch (...) {}
                    }
                }
            }
        } catch (...) {}

        // Write force data (rate limited or change threshold)
        double currentTime = glfwGetTime();
        if (std::abs(forceOutputNewtons - m_lastSentForce) > 0.005f || (currentTime - m_lastSendTime) > 0.05) {
            std::stringstream ss;
            ss << std::fixed << std::setprecision(5) << "F " << forceOutputNewtons << "\n";
            try {
                m_serial->write(ss.str());
                m_lastSentForce = forceOutputNewtons;
                m_lastSendTime = currentTime;
            } catch (...) {}
        }
    }

    [[nodiscard]] float GetPositionMeters() const {
        return m_currentPositionMeters;
    }
};

// --- Sand Simulation ---
class SandSimulation {
private:
    std::vector<Cell> m_grid;
    Cell m_boundaryCell = { MaterialType::Sand, 0 };

    [[nodiscard]] bool IsInBounds(int x, int y) const {
        return x >= 0 && x < width && y >= 0 && y < height;
    }

    [[nodiscard]] int GetIndex(int x, int y) const {
        return y * width + x;
    }

public:
    int width = INITIAL_WIDTH;
    int height = INITIAL_HEIGHT;
    float tickDelayMs = TICK_DELAY_DEFAULT;

    SandSimulation() { Resize(width, height); }

    void Resize(int w, int h) {
        width = w;
        height = h;
        m_grid.assign(width * height, { MaterialType::Empty });
    }

    void Clear() {
        std::fill(m_grid.begin(), m_grid.end(), Cell{ MaterialType::Empty });
    }

    [[nodiscard]] Cell Get(int x, int y) const {
        if (!IsInBounds(x, y)) return m_boundaryCell;
        return m_grid[GetIndex(x, y)];
    }

    void Set(int x, int y, MaterialType type, int soak = 0) {
        if (IsInBounds(x, y)) {
            m_grid[GetIndex(x, y)] = {type, soak};
        }
    }

    bool Move(int x1, int y1, int x2, int y2) {
        if (!IsInBounds(x1, y1) || !IsInBounds(x2, y2)) return false;

        int idx2 = GetIndex(x2, y2);
        if (m_grid[idx2].type != MaterialType::Empty) return false;

        int idx1 = GetIndex(x1, y1);
        m_grid[idx2] = m_grid[idx1];
        m_grid[idx1] = {MaterialType::Empty, 0};
        return true;
    }

    bool Swap(int x1, int y1, int x2, int y2) {
        if (!IsInBounds(x1, y1) || !IsInBounds(x2, y2)) return false;
        std::swap(m_grid[GetIndex(x1, y1)], m_grid[GetIndex(x2, y2)]);
        return true;
    }

    [[nodiscard]] float GetResistance(float cx, float cy, float radius) const {
        float totalResistance = 0.0f;
        float r2 = radius * radius;

        int minX = static_cast<int>(std::floor(cx - radius));
        int maxX = static_cast<int>(std::ceil(cx + radius));
        int minY = static_cast<int>(std::floor(cy - radius));
        int maxY = static_cast<int>(std::ceil(cy + radius));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                if (!IsInBounds(x, y)) continue;

                float dx = static_cast<float>(x) - cx;
                float dy = static_cast<float>(y) - cy;

                if (dx*dx + dy*dy <= r2) {
                    Cell cell = Get(x, y);
                    if (cell.type == MaterialType::Sand) {
                        totalResistance += 0.1f;
                    } else if (cell.type == MaterialType::WetSand) {
                        totalResistance += cell.soak * 0.02f + 0.1f;
                    } else if (cell.type == MaterialType::Water) {
                        totalResistance += 0.02f;
                    }
                }
            }
        }
        return totalResistance;
    }

    [[nodiscard]] glm::ivec2 FindNearestEmpty(int targetX, int targetY, int maxRadius) const {
        if (IsInBounds(targetX, targetY) && Get(targetX, targetY).type == MaterialType::Empty) {
            return glm::ivec2(targetX, targetY);
        }
        for (int r = 1; r <= maxRadius; ++r) {
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::abs(dx) != r && std::abs(dy) != r) continue;

                    int nx = targetX + dx;
                    int ny = targetY + dy;
                    if (IsInBounds(nx, ny) && Get(nx, ny).type == MaterialType::Empty) {
                        return glm::ivec2(nx, ny);
                    }
                }
            }
        }
        return glm::ivec2(-1, -1);
    }

    void Update() {
        for (int y = height - 1; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                MaterialType type = m_grid[y * width + x].type;
                switch (type) {
                    case MaterialType::Sand:    UpdateSand(x, y); break;
                    case MaterialType::WetSand: UpdateWetSand(x, y); break;
                    case MaterialType::Water:   UpdateWater(x, y); break;
                    default: break;
                }
            }
        }
    }

private:
    void UpdateSand(int x, int y) {
        if (y + 1 >= height) return;

        MaterialType below = Get(x, y + 1).type;
        if (below == MaterialType::Water) { Swap(x, y, x, y + 1); return; }
        if (below == MaterialType::Empty) { Move(x, y, x, y + 1); return; }

        bool leftEmpty = (x - 1 >= 0) && Get(x - 1, y + 1).type == MaterialType::Empty;
        bool rightEmpty = (x + 1 < width) && Get(x + 1, y + 1).type == MaterialType::Empty;

        if (leftEmpty && rightEmpty) {
            int offset = (rand() % 2 == 0) ? -1 : 1;
            Move(x, y, x + offset, y + 1);
        } else if (leftEmpty) {
            Move(x, y, x - 1, y + 1);
        } else if (rightEmpty) {
            Move(x, y, x + 1, y + 1);
        }
    }

    void UpdateWetSand(int x, int y) {
        if (y + 1 >= height) return;

        MaterialType below = Get(x, y + 1).type;
        if (below == MaterialType::Empty) { Move(x, y, x, y + 1); return; }
        if (below == MaterialType::Water) { Swap(x, y, x, y + 1); return; }
    }

    void UpdateWater(int x, int y) {
        if (TryWetSand(x, y)) return;

        if (y + 1 < height && Get(x, y + 1).type == MaterialType::Empty) {
            Move(x, y, x, y + 1);
        } else if (y + 1 < height) {
            bool left = (x - 1 >= 0) && Get(x - 1, y + 1).type == MaterialType::Empty;
            bool right = (x + 1 < width) && Get(x + 1, y + 1).type == MaterialType::Empty;

            if (left && right) Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y + 1);
            else if (left) Move(x, y, x - 1, y + 1);
            else if (right) Move(x, y, x + 1, y + 1);
            else {
                bool lSide = (x - 1 >= 0) && Get(x - 1, y).type == MaterialType::Empty;
                bool rSide = (x + 1 < width) && Get(x + 1, y).type == MaterialType::Empty;

                if (lSide && rSide) Move(x, y, (rand() % 2 == 0) ? x - 1 : x + 1, y);
                else if (lSide) Move(x, y, x - 1, y);
                else if (rSide) Move(x, y, x + 1, y);
            }
        }
    }

    bool TryWetSand(int wx, int wy) {
        static const int offsets[9][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {0, 2}};
        for (const auto& o : offsets) {
            int sx = wx + o[0];
            int sy = wy + o[1];

            if (!IsInBounds(sx, sy)) continue;

            Cell cell = Get(sx, sy);
            if (cell.type == MaterialType::Sand) {
                Set(sx, sy, MaterialType::WetSand, 1);
                Set(wx, wy, MaterialType::Empty, 0);
                return true;
            }
            if (cell.type == MaterialType::WetSand && cell.soak < SOAK_THRESHOLD) {
                Set(sx, sy, MaterialType::WetSand, cell.soak + 1);
                Set(wx, wy, MaterialType::Empty, 0);
                return true;
            }
            if (cell.type == MaterialType::WetSand && cell.soak >= SOAK_THRESHOLD && sy < wy) {
                Swap(wx, wy, sx, sy);
                return true;
            }
        }
        return false;
    }
};

// --- Haptic System ---
class HapticSystem {
public:
    enum class AxisMode { X_Axis, Y_Axis };
    enum class ControlMode { Mode_1DOF, Mode_2DOF };

    // State
    glm::vec2 proxyPos  = { 30.0f, 30.0f };
    glm::vec2 devicePos = { 30.0f, 30.0f };
    glm::vec2 anchorPos = { 30.0f, 30.0f };
    float smoothedResistance = 0.0f;
    float currentForce1D = 0.0f;
    float rawInputVal = 0.0f;

    // Configuration
    AxisMode  currentAxis = AxisMode::X_Axis;
    ControlMode currentMode = ControlMode::Mode_1DOF;
    float radius = 4.0f;
    float frictionCoef = 5.0f;
    float hapkitScale = 500.0f;
    float springK = 0.5f;

    void Recenter(const glm::vec2& newCenter) {
        anchorPos = newCenter;
        proxyPos = newCenter;
        devicePos = newCenter;
        rawInputVal = 0.0f;
    }

    void Update(const glm::vec2& mousePos, float rawInputMeters, bool isMouseInput, SandSimulation& sim) {
        if (currentMode == ControlMode::Mode_2DOF) {
            devicePos = mousePos;
            currentForce1D = 0.0f;
        } else {
            if (isMouseInput) {
                if (currentAxis == AxisMode::X_Axis) {
                    rawInputVal = (mousePos.x - anchorPos.x) / hapkitScale;
                } else {
                    rawInputVal = (mousePos.y - anchorPos.y) / hapkitScale;
                }
            } else {
                rawInputVal = rawInputMeters;
            }

            // Clamping
            rawInputVal = std::max(-0.08f, std::min(0.08f, rawInputVal));

            if (currentAxis == AxisMode::X_Axis) {
                devicePos = glm::vec2(anchorPos.x + rawInputVal * hapkitScale, anchorPos.y);
            } else {
                devicePos = glm::vec2(anchorPos.x, anchorPos.y + rawInputVal * hapkitScale);
            }
        }

        // Low Pass Filter on Resistance
        float rawResistance = sim.GetResistance(proxyPos.x, proxyPos.y, radius);
        constexpr float alpha = 0.2f;
        smoothedResistance = smoothedResistance * (1.0f - alpha) + rawResistance * alpha;

        float viscosity = 1.0f / (1.0f + (smoothedResistance * frictionCoef));

        // Move Proxy
        glm::vec2 diff = devicePos - proxyPos;
        proxyPos += diff * viscosity;

        DisplaceSand(sim);

        // Force Calculation (Spring)
        glm::vec2 forceVec = (proxyPos - devicePos) * -springK;

        if (glm::length(forceVec) < 0.025f) forceVec = glm::vec2(0.0f);

        if (currentMode == ControlMode::Mode_1DOF) {
            currentForce1D = (currentAxis == AxisMode::X_Axis) ? forceVec.x : forceVec.y;
        }
    }

    void Render(ImDrawList* draw_list, ImVec2 origin, float cellSize) const {
        ImVec2 sDev = ImVec2(origin.x + devicePos.x * cellSize, origin.y + devicePos.y * cellSize);
        ImVec2 sProx = ImVec2(origin.x + proxyPos.x * cellSize, origin.y + proxyPos.y * cellSize);
        ImVec2 sAnch = ImVec2(origin.x + anchorPos.x * cellSize, origin.y + anchorPos.y * cellSize);

        if (currentMode == ControlMode::Mode_1DOF) {
            ImU32 railColor = IM_COL32(100, 100, 100, 100);
            float railLen = 2000.0f;
            if (currentAxis == AxisMode::X_Axis) {
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

private:
    void DisplaceSand(SandSimulation& sim) {
        int r = static_cast<int>(std::ceil(radius));
        int px = static_cast<int>(proxyPos.x);
        int py = static_cast<int>(proxyPos.y);
        float rSq = radius * radius;

        for (int y = py - r; y <= py + r; ++y) {
            for (int x = px - r; x <= px + r; ++x) {
                if (sim.Get(x, y).type != MaterialType::Empty) {
                    float dx = static_cast<float>(x) - proxyPos.x;
                    float dy = static_cast<float>(y) - proxyPos.y;

                    if (dx*dx + dy*dy <= rSq) {
                        glm::vec2 dir(dx, dy);
                        if (glm::length(dir) < 0.01f) dir = glm::vec2(0, -1);
                        else dir = glm::normalize(dir);

                        glm::vec2 target = proxyPos + dir * (radius + 1.5f);
                        glm::ivec2 best = sim.FindNearestEmpty(static_cast<int>(target.x), static_cast<int>(target.y), 3);
                        if (best.x != -1) sim.Move(x, y, best.x, best.y);
                    }
                }
            }
        }
    }
};

ImU32 GetColor(const Cell& cell) {
    switch (cell.type) {
        case MaterialType::Sand:
            return IM_COL32(235, 200, 100, 255);
        case MaterialType::WetSand:
            return (cell.soak >= SOAK_THRESHOLD)
                   ? IM_COL32(100, 80, 40, 255)
                   : IM_COL32(160, 130, 70, 255);
        case MaterialType::Water:
            return IM_COL32(0, 120, 255, 200);
        case MaterialType::Empty:
        default:
            return IM_COL32(0, 0, 0, 0);
    }
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "SandSim Haptics", nullptr, nullptr);
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

    int currentMaterialIdx = static_cast<int>(MaterialType::Sand);
    char portBuffer[64] = "/dev/ttyUSB0";
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

        // --- Controls Window ---
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Controls");

        ImGui::SliderFloat("Sim Speed (ms)", &sim.tickDelayMs, 1.0f, 200.0f);

        ImGui::RadioButton("Dry", &currentMaterialIdx, static_cast<int>(MaterialType::Sand));
        ImGui::SameLine();
        ImGui::RadioButton("Wet", &currentMaterialIdx, static_cast<int>(MaterialType::WetSand));
        ImGui::SameLine();
        ImGui::RadioButton("H2O", &currentMaterialIdx, static_cast<int>(MaterialType::Water));

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

        if (ImGui::RadioButton("1D (Hapkit/Rail)", haptics.currentMode == HapticSystem::ControlMode::Mode_1DOF))
            haptics.currentMode = HapticSystem::ControlMode::Mode_1DOF;
        ImGui::SameLine();
        if (ImGui::RadioButton("2D (Mouse/Free)", haptics.currentMode == HapticSystem::ControlMode::Mode_2DOF))
            haptics.currentMode = HapticSystem::ControlMode::Mode_2DOF;

        if (haptics.currentMode == HapticSystem::ControlMode::Mode_1DOF) {
            ImGui::Text("Rail Axis:");
            if (ImGui::RadioButton("X-Axis", haptics.currentAxis == HapticSystem::AxisMode::X_Axis))
                haptics.currentAxis = HapticSystem::AxisMode::X_Axis;
            ImGui::SameLine();
            if (ImGui::RadioButton("Y-Axis", haptics.currentAxis == HapticSystem::AxisMode::Y_Axis))
                haptics.currentAxis = HapticSystem::AxisMode::Y_Axis;

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

        // --- Simulation View ---
        ImGui::SetNextWindowSize(ImVec2(600, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Simulation View");

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();

        float cellW = avail.x / static_cast<float>(sim.width);
        float cellH = avail.y / static_cast<float>(sim.height);
        float cellSize = std::min(cellW, cellH);

        // Background
        draw_list->AddRectFilled(p, ImVec2(p.x + sim.width * cellSize, p.y + sim.height * cellSize), IM_COL32(255, 255, 255, 255));

        // Grid Lines
        ImU32 gridCol = IM_COL32(220, 220, 220, 255);
        for (int i = 0; i <= sim.width; ++i)
            draw_list->AddLine(ImVec2(p.x + i * cellSize, p.y), ImVec2(p.x + i * cellSize, p.y + sim.height * cellSize), gridCol);
        for (int i = 0; i <= sim.height; ++i)
            draw_list->AddLine(ImVec2(p.x, p.y + i * cellSize), ImVec2(p.x + sim.width * cellSize, p.y + i * cellSize), gridCol);

        // Particles
        for (int y = 0; y < sim.height; ++y) {
            for (int x = 0; x < sim.width; ++x) {
                Cell c = sim.Get(x, y);
                if (c.type != MaterialType::Empty) {
                    ImVec2 min = ImVec2(p.x + x * cellSize, p.y + y * cellSize);
                    ImVec2 max = ImVec2(min.x + cellSize, min.y + cellSize);
                    draw_list->AddRectFilled(min, max, GetColor(c));
                }
            }
        }

        // Interactions
        if (ImGui::IsWindowHovered()) {
            ImVec2 m = ImGui::GetMousePos();
            glm::vec2 mouseGridPos;
            mouseGridPos.x = (m.x - p.x) / cellSize;
            mouseGridPos.y = (m.y - p.y) / cellSize;

            if (ImGui::IsKeyPressed(ImGuiKey_G)) {
                haptics.Recenter(mouseGridPos);
            }

            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                auto type = static_cast<MaterialType>(currentMaterialIdx);
                int initialSoak = (type == MaterialType::WetSand) ? SOAK_THRESHOLD : 0;
                sim.Set(static_cast<int>(mouseGridPos.x), static_cast<int>(mouseGridPos.y), type, initialSoak);
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