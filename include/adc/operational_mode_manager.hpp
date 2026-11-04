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
//
// ---- Module Interface Specification (MG 1.2.3) --------------------------------
// Responsibility: runs D1 State Machine 1 (SM1) and says what the current mode permits.
// Secret:         the SM1 transition table [mode][event] (src/operational_mode_manager.cpp),
//                 the entry action of each mode, and the mode -> permitted-operation rules (C1).
// Uses:           AccessControlManager, FaultDetector, EventLogger, HardwareInterface (indicator).
// State invariant(s):
//   INV-MODE-1: mode() == Normal  =>  !faults.hasCriticalFault()   (NFR-SAF-002)
//   INV-MODE-2: dispensingAllowed()  <=>  mode() == Normal and no critical fault and the
//               fail-safe has not tripped (C1, FR-DISP-005, FR-MODE-005)
//   INV-MODE-3: every mode change has exactly one MODE_CHANGED record (FR-LOG-003).
// Upward notification: mode and maintenance listeners (AdcSystem -> CommunicationManager).

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "adc/access_control_manager.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/types.hpp"

namespace adc {

class OperationalModeManager {
public:
    using ModeListener = std::function<void(Mode from, Mode to, const std::string& reason)>;
    using MaintenanceListener = std::function<void(const std::string& activity, const Badge& by)>;

    // OperationalModeManager(access, faults, logger, hardware)
    // Post:        mode() == Startup.
    OperationalModeManager(AccessControlManager& access, FaultDetector& faults, EventLogger& logger,
                           HardwareInterface& hardware);

    // mode() -> current SM1 state.
    Mode mode() const;

    // dispensingAllowed() -> INV-MODE-2 (clarification C1).
    // Reqs:        FR-DISP-005, FR-MODE-005, NFR-SAF-001, NFR-SAF-002
    bool dispensingAllowed() const;

    // replenishmentAllowed() -> true iff mode is Normal or Maintenance, no critical fault and the
    //                           fail-safe has not tripped (C1).
    bool replenishmentAllowed() const;

    // completeStartup(selfTestFaults)
    // Reqs:        NFR-REL-004, US-REL-01 AC-03/AC-04
    // Pre:         selfTestFaults is the result of FaultDetector::selfTest.
    // Post:        fires SM1 startupChecksPassed if selfTestFaults is empty and no critical fault
    //              is active, startupCheckFailed otherwise (Startup -> Normal / Fault).
    // Semantics:   SM1 table row Startup.
    // Effects:     MODE_CHANGED; indicator updated; mode listeners.
    void completeStartup(const std::vector<Fault>& selfTestFaults);

    // requestEnterMaintenance() -> OpResult
    // Reqs:        FR-MODE-003, FR-MODE-004, NFR-AUD-004
    // Post:        SM1 enterMaintenance: Normal + [authorizedForMaintenance] -> Maintenance;
    //              otherwise no change and a rejection.
    // Effects:     MODE_CHANGED + MAINTENANCE_ACTIVITY + maintenance listeners on success;
    //              ACCESS_DENIED (not authorised) or UNEXPECTED_EVENT (wrong mode) on rejection.
    OpResult requestEnterMaintenance();

    // requestExitMaintenance() -> OpResult
    // Reqs:        FR-MODE-003, FR-MODE-004, NFR-AUD-004
    // Post:        SM1 exitMaintenance: Maintenance + [authorizedForMaintenance] -> Normal.
    // Effects:     as requestEnterMaintenance.
    OpResult requestExitMaintenance();

    // runDiagnostics(expectedCounts) -> OpResult
    // Reqs:        use case "Run Diagnostic Check", NFR-AUD-004, FR-COM-002
    // Semantics:   function table MODE-DIAG: R1 not authorised (RunDiagnostics) -> denied;
    //              R2 mode not Maintenance or Fault -> refused; R3 otherwise -> run
    //              FaultDetector::diagnose and report the findings.
    // Effects:     R3: MAINTENANCE_ACTIVITY with the findings + maintenance listeners. No mode change.
    OpResult runDiagnostics(const std::map<DrawerId, int>& expectedCounts);

    // requestClearFault(expectedCounts) -> OpResult
    // Reqs:        NFR-SAF-002, FR-MODE-005, use case "Acknowledge/Clear Fault"
    // Post:        SM1 faultCleared: Fault + [authorizedUser and all critical faults gone] -> Normal;
    //              Fault + otherwise -> stay Fault, denial logged. Other modes: Ignore.
    // Effects:     ACCESS_DENIED, or FaultDetector::tryClear records (FAULT_CLEARED /
    //              FAULT_CLEAR_DENIED), then MODE_CHANGED on success.
    OpResult requestClearFault(const std::map<DrawerId, int>& expectedCounts);

    // onFaultDetected(fault)   (FaultDetector listener)
    // Reqs:        FR-MODE-005, NFR-SAF-001, C6
    // Post:        critical fault: SM1 faultDetected (Startup/Normal/Maintenance -> Fault; Fault
    //              stays Fault, the fault is already logged by FaultDetector). Non-critical
    //              (CommunicationLoss): no SM1 event, indicator becomes WARNING.
    void onFaultDetected(const Fault& fault);

    // powerOff()
    // Post:        SM1 powerOff: mode() == Off.  Effects: MODE_CHANGED, SYSTEM_SHUTDOWN.
    void powerOff();

    // refreshIndicator()
    // Reqs:        NFR-USB-003, FR-FAULT-002
    // Post:        o_setIndicator shows: Fault -> FAULT + first critical fault message;
    //              Maintenance / Startup -> WARNING; Normal with a non-critical fault -> WARNING +
    //              its message; Normal otherwise -> OK "Ready".
    void refreshIndicator();

    void addModeListener(ModeListener listener);
    void addMaintenanceListener(MaintenanceListener listener);

private:
    using Guard = std::function<OpResult()>;
    OpResult fire(ModeEvent event, const std::string& reason, const Guard& guard);
    void changeMode(Mode to, const std::string& actor, const std::string& reason);
    void enter(Mode state);
    void failSafe(ModeEvent event);
    std::string actor() const;

    AccessControlManager& access_;
    FaultDetector& faults_;
    EventLogger& logger_;
    HardwareInterface& hardware_;
    Mode mode_ = Mode::Startup;
    bool failSafeTripped_ = false;
    std::vector<ModeListener> modeListeners_;
    std::vector<MaintenanceListener> maintenanceListeners_;
};

}  // namespace adc
