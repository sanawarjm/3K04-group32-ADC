#pragma once
// D1 component: Communication Manager
// Responsibility:
//   Sends status, inventory, alarms, maintenance information and events to the Monitoring
//   System, detects loss of communication, keeps operating locally, and resynchronizes when the
//   link returns (D1 State Machine 3).
// D1 user stories:
//   US-COM-01, US-AVL-01, US-PRF-01 (AC-04), US-FAULT-01 (AC-03)
// D1 requirements traced to this component (D1 section 6):
//   FR-LOG-004, FR-FAULT-003, FR-COM-001, FR-COM-002, FR-COM-003, FR-COM-004, FR-COM-005, NFR-
//   REL-002, NFR-AVL-001, NFR-AVL-002, NFR-AVL-003, NFR-AVL-004, NFR-PRF-003
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/communication_manager.cpp.

namespace adc {

class CommunicationManager {
public:
    CommunicationManager() = default;
    // TODO (D2): exported operations
private:
    // Hidden implementation details live in src/communication_manager.cpp
};

}  // namespace adc
