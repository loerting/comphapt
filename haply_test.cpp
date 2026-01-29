#include <iostream>
#include <thread>
#include <chrono>

// Include the official headers
// (If these show as red, ensure CMake loaded the include directories correctly)
#include "Haply/src/Haply/Device.h"
#include "Haply/src/Haply/HaplyTwoDoFMech.h"
#include "Haply/src/Haply/Board.h"

int main() {
    const char* portName = "/dev/ttyACM0";
    int DEVICE_ID = 2; // We send to 2

    std::cout << "--- HAPLY OFFICIAL API (v0.1.0) ---" << std::endl;
    std::cout << "Connecting to " << portName << "..." << std::endl;

    // 1. Setup Board
    // The library handles Serial opening and flushing internally
    Haply::Board board(portName, 0);

    // 2. Setup Mechanics & Device
    Haply::HaplyTwoDoFMech mech;
    Haply::Device haply(DEVICE_ID, &board, &mech);

    // 3. Setup Handshake
    // v0.1.0 Device constructor might not auto-send setup, so we do it manually if needed,
    // but usually Device() handles it. Let's force a parameter set just in case.
    std::cout << "Sending device parameters..." << std::endl;
    haply.set_mechanism_parameters(mech.get_coordinate()); // Dummy update to force comms

    // Give it a moment to settle
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Starting Loop..." << std::endl;

    while (true) {
        if (board.data_available()) {

            // 1. Read Data
            // Device::device_read_data() or similar usually handles the receive
            // But usually we just call get_device_angles() which triggers the read
            float angles[2];
            haply.get_device_angles(angles);

            float pos[2];
            haply.get_device_position(pos);

            // 2. Physics (Virtual Wall)
            float fx = 0, fy = 0;
            if (pos[0] > 0.05) {
                fx = -300.0 * (pos[0] - 0.05);
            }

            // 3. Write Torques
            float torques[2];
            float forces[2] = {fx, fy};
            mech.torqueCalculation(forces, torques);

            haply.set_device_torques(torques);
            haply.device_write_torques();

            // Debug
            printf("ID:%d | Pos: (%.4f, %.4f) | Force: %.2f\n", DEVICE_ID, pos[0], pos[1], fx);
        } else {
            // If no data, send a request (Keep Alive)
            // Some versions require you to explicitly request data if not streaming
             haply.device_read_request();
        }

        // 1kHz loop roughly
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    return 0;
}