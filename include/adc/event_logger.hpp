#pragma once
// D1 component: Event Logger
// Responsibility:
//   Records dispensing outcomes, authentication attempts, inventory changes, faults, mode
//   changes and maintenance activity in chronological order, and retains them locally.
// D1 user stories:
//   US-LOG-01, US-AUD-01, US-REL-01 (AC-01, AC-02), US-AVL-01 (AC-02)
// D1 requirements traced to this component (D1 section 6):
//   FR-AUTH-004, FR-INV-004, FR-LOG-001, FR-LOG-002, FR-LOG-003, FR-LOG-004, FR-COM-003, FR-
//   COM-005, NFR-REL-001, NFR-REL-002, NFR-AVL-002, NFR-AVL-003, NFR-AUD-001, NFR-AUD-002, NFR-
//   AUD-003, NFR-AUD-004, NFR-AUD-005
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/event_logger.cpp.

namespace adc {

class EventLogger {
public:
    EventLogger() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/event_logger.cpp
};

}  // namespace adc
