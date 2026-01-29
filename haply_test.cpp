#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

// 1. Include the headers properly
#include "Haply/Device.h"
#include "Haply/HaplyTwoDoFMech.h"
#include "Haply/Board.h"
#include "Wjwwood/WjwwoodSerial.h" // Required for Serial implementation

using namespace Haply; // Namespace defined in library files

int main() {
    // 2. Setup Serial Connection
    // The Board class requires a Serial object pointer
    WjwwoodSerial serial;

    // Attempt to open connection (WjwwoodSerial auto-detects ports in this version)
    if (!serial.open()) {
        std::cerr << "Failed to open serial port!" << std::endl;
        return -1;
    }

    std::cout << "--- HAPLY OFFICIAL API (v0.1.0) ---" << std::endl;

    // 3. Setup Board
    // Board constructor takes a Serial*
    Board board(&serial);

    // 4. Setup Device
    // Constructor: Device(DeviceType, deviceID, Board*)
    // The mechanism is created internally based on DeviceType::HaplyTwoDOF
    Device haply(DeviceType::HaplyTwoDOF, 2, &board);

    std::cout << "Sending device parameters..." << std::endl;

    // Give it a moment to settle
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Starting Loop..." << std::endl;

    while (true) {
        if (board.data_available()) { //

            // 5. Read Data
            // get_device_position returns a std::vector<float>, not a raw array
            std::vector<float> angles = haply.get_device_angles();
            std::vector<float> pos = haply.get_device_position(angles);

            // 6. Physics (Virtual Wall Example)
            float fx = 0, fy = 0;
            // Access vector elements using [0] and [1]
            if (pos[0] > 0.05) {
                fx = -300.0 * (pos[0] - 0.05);
            }

            // 7. Write Torques
            std::vector<float> forces = {fx, fy};

            // set_device_torques calculates AND sets the motor torques internally
            haply.set_device_torques(forces);

            // Transmits the stored torques to the board
            haply.device_write_torques();

            // Debug
            printf("Pos: (%.4f, %.4f) | Force: %.2f\n", pos[0], pos[1], fx);
        }

        // 1kHz loop roughly
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    return 0;
}