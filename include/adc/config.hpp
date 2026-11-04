#pragma once
// Every tunable number of the ADC lives here (Week 7 drift check: no magic numbers elsewhere).
// D1 section 7 says limits are estimates; each value names its D1 source or says
// "team estimate (D1 §7)".
//
// ---- Module Interface Specification (MG 1.3.5) --------------------------------
// Responsibility: holds the configuration of one cabinet, including the demo seed data.
// Secret:         the chosen values of every limit, timeout and demo record.
// Uses:           types (Medication).
// State invariant(s): INV-CFG-1: every duration > 0; heartbeatPeriod < commLossTimeout;
//                     commLossTimeout + tickPeriod < 5 s (NFR-PRF-003).
//
// AdcConfig is a plain value record handed to the composition root (AdcSystem); modules keep a
// copy and never change it, except CommunicationManager's heartbeat (clarification C8).

#include <chrono>
#include <string>
#include <vector>

#include "adc/types.hpp"

namespace adc {

using Millis = std::chrono::milliseconds;

struct AdcConfig {
    // Cabinet identifier sent in every message (interface/adc-monitoring-interface.md).
    std::string cabinetId = "ADC-01";

    // NFR-SEC-003 "team-defined" timeout; clarification C2.
    Millis sessionTimeout{60000};
    // Time to take medication after unlock (FR-DISP-007): team estimate (D1 §7).
    Millis retrievalTimeout{30000};
    // Time to close the drawer after removal (D1 SM2 closureTimeout): team estimate (D1 §7).
    Millis closureTimeout{20000};
    // An unlocked, unauthorised drawer must relock within this (US-FAULT-01 AC-02): team estimate (D1 §7).
    Millis lockConfirmTimeout{2000};
    // No successful exchange for this long = communication lost; clarification C3 (< 5 s, NFR-PRF-003).
    Millis commLossTimeout{3000};
    // Heartbeat / retry period of the Monitoring link; clarification C3. Updatable (C8).
    Millis heartbeatPeriod{1000};
    // Period of the control loop; one tick bounds NFR-PRF-002 inventory latency: team estimate (D1 §7).
    Millis tickPeriod{200};
    // Folder holding every persisted file (NFR-REL-001).
    std::string dataDir = "data";

    // Most messages sent to the Monitoring System in one exchange: team estimate (D1 §7).
    int maxBatchSize = 25;
    // Number of log records shown on screen by "View event log": team estimate (D1 §7).
    int recentEventsShown = 20;

    // Drawers of the cabinet (D1 section 1.2: hardware exists; four for the demo).
    std::vector<DrawerId> drawers{"D1", "D2", "D3", "D4"};

    // Demo drug library, seeded on the first run only (one medication per drawer).
    std::vector<Medication> seedInventory{
        {"M01", "Acetaminophen 500 mg tablet", "D1", 20},
        {"M02", "Ondansetron 4 mg tablet", "D2", 15},
        {"M03", "Morphine 2 mg/mL injection", "D3", 10},
        {"M04", "Heparin 5,000 units/mL injection", "D4", 12},
    };
};

}  // namespace adc
