#pragma once
// D1 component: Operational Mode Manager
// Responsibility:
//   Holds the operational mode (Startup, Normal, Maintenance, Fault) and applies the
//   transitions of D1 State Machine 1.
// D1 user stories:
//   US-MODE-01, US-SAF-01 (AC-02), US-REL-01 (AC-03, AC-04)
// D1 requirements traced to this component (D1 section 6):
//   FR-DISP-005, FR-LOG-003, FR-MODE-001, FR-MODE-002, FR-MODE-003, FR-MODE-004, FR-MODE-005,
//   NFR-SAF-001, NFR-SAF-002, NFR-SEC-002, NFR-REL-004, NFR-AVL-001, NFR-AVL-004, NFR-AUD-004,
//   NFR-USB-003, NFR-USB-004
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/operational_mode_manager.cpp.

namespace adc {

class OperationalModeManager {
public:
    OperationalModeManager() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/operational_mode_manager.cpp
};

}  // namespace adc
