#pragma once
// Boundary: Cabinet hardware (the "Hardware" swimlane in the D1 activity diagrams)
// D1 scope (section 1.2): drawers, locks, sensors and actuators already exist and
// provide software interfaces the ADC communicates with. Their mechanical design is
// out of scope.
// This header is where the ADC meets that hardware: drawer access (unlock / relock),
// removal and quantity sensing, and the local warning indicators. For the D2 demo a
// simulated implementation can sit behind the same interface.
// D1 requirements that depend on it: FR-DISP-003, FR-DISP-004, FR-FAULT-002,
// NFR-REL-004, NFR-USB-003.

namespace adc {

class HardwareInterface {
public:
    HardwareInterface() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/hardware_interface.cpp
};

}  // namespace adc
