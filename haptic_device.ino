#include <Arduino.h>
#include <math.h>

// --- Configuration Constants ---
constexpr int PIN_PWM = 5;
constexpr int PIN_DIR = 8;
constexpr int PIN_SENSOR = A2;

// Kinematics (meters)
constexpr float RADIUS_PULLEY = 0.004191f;
constexpr float RADIUS_SECTOR = 0.073152f;
constexpr float RADIUS_HANDLE = 0.065659f;

// Motor & Calibration
constexpr float MOTOR_CONST = 0.03f; // Approximate torque constant
constexpr float CALIB_SLOPE = 0.013976f;
constexpr float CALIB_OFFSET = 4.219075f;
constexpr int FLIP_THRESHOLD = 700;

// --- State Management ---
struct SensorState {
    int rawPos = 0;
    int lastRawPos = 0;
    int lastLastRawPos = 0;
    int flipNumber = 0;
    int tempOffset = 0;
    bool flipped = false;
    int updatedPos = 0;
};

SensorState sensor;
float handlePosMeters = 0.0f;
float forceOutput = 0.0f;

// --- Function Prototypes ---
void updateSensorState();
void calculatePhysics();
void writeMotorForce(float force);
void setPwmFrequency(int pin, int divisor);

void setup() {
    Serial.begin(115200);

    pinMode(PIN_SENSOR, INPUT);
    pinMode(PIN_PWM, OUTPUT);
    pinMode(PIN_DIR, OUTPUT);

    // Set PWM to ~31kHz to eliminate audible whine
    setPwmFrequency(PIN_PWM, 1);

    // Initialize sensor history to prevent startup jumps
    sensor.lastLastRawPos = analogRead(PIN_SENSOR);
    sensor.lastRawPos = analogRead(PIN_SENSOR);
    sensor.flipNumber = 0;
}

void loop() {
    // 1. Read & Calculate
    updateSensorState();
    calculatePhysics();

    // 2. Report Position
    Serial.print("P ");
    Serial.println(handlePosMeters, 4);

    // 3. Process Incoming Forces
    // Consume all bytes to ensure we use the most recent 'F' command
    while (Serial.available() > 0) {
        char c = Serial.peek();
        if (c == 'F') {
            Serial.read(); // Consume 'F'
            float val = Serial.parseFloat();
            forceOutput = constrain(val, -5.0f, 5.0f);
        } else {
            Serial.read(); // Discard garbage
        }
    }

    // 4. Actuate
    writeMotorForce(forceOutput);
}

void updateSensorState() {
    sensor.rawPos = analogRead(PIN_SENSOR);
    
    int rawDiff = sensor.rawPos - sensor.lastRawPos;
    int lastRawDiff = sensor.rawPos - sensor.lastLastRawPos;
    
    int localRawOffset = std::abs(rawDiff);
    int localLastRawOffset = std::abs(lastRawDiff);

    sensor.lastLastRawPos = sensor.lastRawPos;
    sensor.lastRawPos = sensor.rawPos;

    // Handle magnetic sector flips
    if ((localLastRawOffset > FLIP_THRESHOLD) && !sensor.flipped) {
        sensor.flipNumber += (lastRawDiff > 0) ? -1 : 1;
        
        if (localRawOffset > FLIP_THRESHOLD) {
            sensor.updatedPos = sensor.rawPos + sensor.flipNumber * localRawOffset;
            sensor.tempOffset = localRawOffset;
        } else {
            sensor.updatedPos = sensor.rawPos + sensor.flipNumber * localLastRawOffset;
            sensor.tempOffset = localLastRawOffset;
        }
        sensor.flipped = true;
    } else {
        sensor.updatedPos = sensor.rawPos + sensor.flipNumber * sensor.tempOffset;
        sensor.flipped = false;
    }
}

void calculatePhysics() {
    // Convert ADC steps to degrees
    float thetaDegrees = CALIB_SLOPE * sensor.updatedPos - CALIB_OFFSET;
    
    // Convert degrees to meters
    handlePosMeters = RADIUS_HANDLE * (thetaDegrees * static_cast<float>(M_PI) / 180.0f);
}

void writeMotorForce(float force) {
    // Gear reduction torque calculation
    float torque = (RADIUS_PULLEY / RADIUS_SECTOR) * RADIUS_HANDLE * force;

    digitalWrite(PIN_DIR, (force < 0) ? HIGH : LOW);

    // Non-linear duty cycle mapping
    float duty = std::sqrt(std::abs(torque) / MOTOR_CONST);
    duty = constrain(duty, 0.0f, 1.0f);

    analogWrite(PIN_PWM, static_cast<int>(duty * 255));
}

// Low-level register manipulation for AVR ATMega328P (Uno/Nano)
void setPwmFrequency(int pin, int divisor) {
    byte mode;
    if (pin == 5 || pin == 6 || pin == 9 || pin == 10) {
        switch (divisor) {
            case 1: mode = 0x01; break;
            case 8: mode = 0x02; break;
            case 64: mode = 0x03; break;
            case 256: mode = 0x04; break;
            case 1024: mode = 0x05; break;
            default: return;
        }
        if (pin == 5 || pin == 6) {
            TCCR0B = (TCCR0B & 0b11111000) | mode;
        } else {
            TCCR1B = (TCCR1B & 0b11111000) | mode;
        }
    }
}