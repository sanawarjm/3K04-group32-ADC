#pragma once
// D1 component: Fault Detector
// Responsibility:
//   Detects and identifies faults (hardware, incomplete dispensing, loss of Monitoring System
//   communication) and reports them so dispensing can be stopped.
// D1 user stories:
//   US-FAULT-01, US-SAF-01 (AC-01), US-USB-01 (AC-02, AC-03)
// D1 requirements traced to this component (D1 section 6):
//   FR-LOG-002, FR-FAULT-001, FR-FAULT-002, FR-FAULT-003, FR-FAULT-004, FR-MODE-005, FR-
//   COM-002, NFR-SAF-001, NFR-REL-004, NFR-PRF-003, NFR-AUD-004, NFR-USB-002, NFR-USB-003
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/fault_detector.cpp.

namespace adc {

class FaultDetector {
public:
    FaultDetector() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/fault_detector.cpp
};

}  // namespace adc
