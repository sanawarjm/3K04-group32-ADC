#pragma once
// D1 component: Dispensing Controller
// Responsibility:
//   Runs one dispensing request from start to finish: accepts the request, finds the storage
//   location, provides access, detects removal, and prevents a second dose from the same
//   request (D1 State Machine 2).
// D1 user stories:
//   US-DISP-01, US-SAF-01, US-INV-01 (AC-02)
// D1 requirements traced to this component (D1 section 6):
//   FR-DISP-001, FR-DISP-002, FR-DISP-003, FR-DISP-004, FR-DISP-005, FR-DISP-006, FR-DISP-007,
//   FR-LOG-001, FR-FAULT-001, FR-FAULT-004, FR-MODE-005, NFR-SAF-001, NFR-SAF-002, NFR-SAF-003,
//   NFR-REL-003, NFR-REL-004, NFR-AVL-004, NFR-PRF-002, NFR-AUD-001, NFR-AUD-002, NFR-USB-001,
//   NFR-USB-004
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/dispensing_controller.cpp.

namespace adc {

class DispensingController {
public:
    DispensingController() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/dispensing_controller.cpp
};

}  // namespace adc
