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
//
// ---- Module Interface Specification (MG 1.3.3) --------------------------------
// Responsibility: keeps the set of active faults: raises them from hardware readings and from
//                 other modules' reports, and clears them only when their condition is gone.
// Secret:         the fault rules (function table FLT-CHECK), thresholds, which faults are
//                 critical, the plain-language messages, and the clear conditions.
// Uses:           HardwareInterface, EventLogger, Clock, AdcConfig, types.
// State invariant(s):
//   INV-FLT-1: at most one active fault per (type, drawer) pair (report is idempotent).
//   INV-FLT-2: every fault that became active has exactly one FAULT_DETECTED record (FR-LOG-002).
//   INV-FLT-3: CommunicationLoss is never critical; every other fault type is critical.
// Upward notification: listeners registered by AdcSystem (OperationalModeManager,
// DispensingController, CommunicationManager). A listener is not a "use".

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/event_logger.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/types.hpp"

namespace adc {

class FaultDetector {
public:
    using Listener = std::function<void(const Fault&)>;

    // FaultDetector(hardware, logger, clock, config)
    // Pre:         the referenced objects outlive this one.
    // Post:        no active faults.
    FaultDetector(HardwareInterface& hardware, EventLogger& logger, const Clock& clock, const AdcConfig& config);

    // report(type, drawer, detail) -> bool
    // Reqs:        FR-FAULT-001, FR-FAULT-002, FR-FAULT-004, FR-LOG-002, NFR-SAF-001
    // Pre:         detail holds no confidential data (NFR-USB-002).
    // Post:        a fault (type, drawer) is active. Returns true iff it was not active before.
    // Effects:     when newly active: FAULT_DETECTED record, then every listener is called with
    //              the fault. Nothing happens if it was already active (INV-FLT-1).
    // Exceptions:  none.
    bool report(FaultType type, const DrawerId& drawer, const std::string& detail);

    // check(authorizedDrawers, expectedCounts)
    // Reqs:        FR-FAULT-001, FR-FAULT-002, NFR-SAF-001, NFR-REL-001
    // Pre:         called every tickPeriod; authorizedDrawers are the drawers under an active
    //              dispensing/replenish session; expectedCounts are the recorded quantities.
    // Post:        each row of function table FLT-CHECK (docs/specification.md) that holds has
    //              been reported.
    // Semantics:   per drawer d, first true row wins:
    //              R1 a sensor of d gives no reading                         -> SensorFailure
    //              R2 d open and d not authorized                            -> UnauthorizedAccess
    //              R3 d closed, lock disengaged, not authorized, for >= lockConfirmTimeout
    //                                                                        -> StorageNotClosed
    //              R4 d closed, locked, not authorized, count != expected    -> IncompleteDispense
    //              R5 otherwise                                              -> no fault
    //              and once per call: R6 EventLogger::healthy() == false     -> StorageFailure
    // Effects:     as report().
    void check(const std::set<DrawerId>& authorizedDrawers, const std::map<DrawerId, int>& expectedCounts);

    // diagnose(expectedCounts) -> findings
    // Reqs:        NFR-REL-004, US-REL-01 AC-03/04, use case "Run Diagnostic Check"
    // Post:        one plain-language finding per drawer that is not closed, locked, reading
    //              all sensors and holding its recorded count; empty means every check passed.
    // Effects:     none (pure query of the hardware).
    std::vector<std::string> diagnose(const std::map<DrawerId, int>& expectedCounts) const;

    // selfTest(expectedCounts) -> faults
    // Reqs:        NFR-REL-004, US-REL-01 AC-03/AC-04
    // Pre:         every lock has just been commanded LOCKED (AdcSystem start-up).
    // Post:        one StartupCheckFailed fault per failing drawer is active and returned;
    //              empty means the start-up check passed.
    // Effects:     FAULT_DETECTED record per new fault. Listeners are NOT called: the result is
    //              delivered to SM1 as startupCheckFailed by OperationalModeManager::completeStartup.
    std::vector<Fault> selfTest(const std::map<DrawerId, int>& expectedCounts);

    // tryClear(actor, expectedCounts) -> OpResult
    // Reqs:        NFR-SAF-002, use case "Acknowledge/Clear Fault"
    // Pre:         the caller has already authorized actor for ClearFault.
    // Post:        every critical fault whose condition is gone on the hardware is no longer
    //              active. Ok iff no critical fault remains; FaultStillPresent otherwise.
    // Effects:     FAULT_CLEARED per removed fault, FAULT_CLEAR_DENIED per remaining one.
    OpResult tryClear(const Badge& actor, const std::map<DrawerId, int>& expectedCounts);

    // reportCommunicationLoss() / clearCommunicationLoss()
    // Reqs:        FR-FAULT-003, NFR-PRF-003, US-FAULT-01 AC-03
    // Post:        the non-critical CommunicationLoss fault is active / not active.
    // Effects:     FAULT_DETECTED (+ listeners) / FAULT_CLEARED, only on an actual change.
    void reportCommunicationLoss();
    void clearCommunicationLoss();

    // hasCriticalFault() -> true iff any critical fault is active.
    bool hasCriticalFault() const;

    // activeFaults() -> copy of the active faults, in the order they were raised.
    std::vector<Fault> activeFaults() const;

    // isActive(type, drawer) -> true iff that fault is active.
    bool isActive(FaultType type, const DrawerId& drawer) const;

    // addListener(listener): called after every newly active fault (except selfTest results).
    void addListener(Listener listener);

    // isCritical(type) -> INV-FLT-3.
    static bool isCritical(FaultType type);

    // messageFor(type, drawer) -> plain-language screen text (NFR-USB-002), no confidential data.
    static std::string messageFor(FaultType type, const DrawerId& drawer);

private:
    bool raise(FaultType type, const DrawerId& drawer, const std::string& detail, bool notify);
    bool drawerHealthy(const DrawerId& d, const std::map<DrawerId, int>& expectedCounts, std::string* why) const;
    bool conditionGone(const Fault& fault, const std::map<DrawerId, int>& expectedCounts);
    void remove(FaultType type, const DrawerId& drawer);

    HardwareInterface& hardware_;
    EventLogger& logger_;
    const Clock& clock_;
    std::chrono::milliseconds lockConfirmTimeout_;
    std::vector<Fault> active_;
    std::map<DrawerId, std::chrono::milliseconds> unlockedSince_;
    std::vector<Listener> listeners_;
};

}  // namespace adc
