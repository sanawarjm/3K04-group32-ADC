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
//
// ---- Module Interface Specification (MG 1.2.2) --------------------------------
// Responsibility: runs D1 State Machine 2 (SM2) for every dispense and replenish session.
// Secret:         the SM2 transition table [state][event], how sensor readings become the SM2
//                 events medicationRemoved / compartmentClosed, the session timers, and how an
//                 active session is persisted so a restart can never resume it.
// Uses:           AccessControlManager, InventoryManager, OperationalModeManager, FaultDetector,
//                 HardwareInterface, EventLogger, Storage (active session), Clock, AdcConfig.
//                 (Storage is added to the section 3 list: needed for NFR-SAF-003 persistence.)
// State invariant(s):
//   INV-DSP-1: at most one drawer is commanded UNLOCKED by this module, and only while a
//              session is in AwaitingRetrieval or AwaitingClosure for that drawer.
//   INV-DSP-2: every attempt (every call of requestDispense / requestReplenish) produces exactly
//              one DISPENSE_OUTCOME / REPLENISH_OUTCOME record.
//   INV-DSP-3: a session in a terminal state never re-enters an active state; a new request
//              starts a new session (FR-DISP-006, NFR-SAF-003).
//   INV-DSP-4: every change of the sensed count of a drawer under control is recorded in the
//              inventory within one tick (NFR-PRF-002, NFR-REL-003).

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/storage.hpp"
#include "adc/types.hpp"

namespace adc {

enum class SessionKind { Dispense, Replenish };

// What the screen needs to know about the current or last session. A value record.
struct SessionInfo {
    SessionKind kind = SessionKind::Dispense;
    DispenseState state = DispenseState::Idle;
    Badge user;
    MedicationId medication;
    DrawerId drawer;
    int requested = 0;  // units requested (dispense only)
    int moved = 0;      // units removed (dispense) or added (replenish) so far
    std::chrono::milliseconds timeLeft{0};  // until the running SM2 timer fires
    bool drawerUnderControl = false;        // the drawer is still tracked until it is closed
};

// One cell of a state machine table, as text, for tests and documentation. A value record.
// kind is one of "Go", "Guarded", "Stay", "Ignore", "Reject", "Illegal".
struct DispenseCell {
    std::string kind;
    DispenseState target = DispenseState::Idle;
    std::string actions;
};

class DispensingController {
public:
    using OutcomeListener = std::function<void(const SessionInfo& session, const std::string& reason)>;

    // DispensingController(access, inventory, modes, faults, hardware, logger, storage, clock, config)
    // Pre:         the referenced objects outlive this one.
    // Post:        state() == Idle; no drawer under control.
    DispensingController(AccessControlManager& access, InventoryManager& inventory, OperationalModeManager& modes,
                         FaultDetector& faults, HardwareInterface& hardware, EventLogger& logger, Storage& storage,
                         const Clock& clock, const AdcConfig& config);

    // requestDispense(medication, quantity) -> OpResult
    // Reqs:        FR-DISP-001, FR-DISP-002, FR-DISP-003, FR-DISP-005, FR-AUTH-003, NFR-SAF-001,
    //              NFR-AUD-001, NFR-AUD-002, NFR-USB-001, US-DISP-01 AC-02
    // Pre:         none (every input is checked).
    // Post:        Ok: state() == AwaitingRetrieval, only drawer locationOf(medication) is
    //              commanded UNLOCKED, the baseline count is stored and the retrieval timer runs.
    //              Otherwise the attempt is Rejected and any running session is unchanged.
    // Semantics:   SM2 dispenseRequest, then function table DSP-REQ in docs/specification.md
    //              (rows R1..R8, first true row wins).
    // Effects:     exactly one DISPENSE_OUTCOME record when rejected (INV-DSP-2); ACCESS_DENIED
    //              for R1; o_setLock(drawer, UNLOCKED) for R8; session persisted.
    // Exceptions:  none; every refusal is an OpResult with a plain message.
    OpResult requestDispense(const MedicationId& medication, int quantity);

    // requestReplenish(medication) -> OpResult
    // Reqs:        FR-INV-003, FR-AUTH-003, C1
    // Semantics:   function table REP-REQ (DSP-REQ with Replenish permission, no quantity rows,
    //              and replenishmentAllowed() for R5). Units added are measured by the count sensor.
    // Effects:     REPLENISH_OUTCOME record when rejected; otherwise as requestDispense.
    OpResult requestReplenish(const MedicationId& medication);

    // cancel() -> OpResult
    // Reqs:        FR-DISP-006 (D1 SM2 cancelRequest)
    // Post:        AwaitingRetrieval -> Cancelled (drawer commanded LOCKED). AwaitingClosure: no
    //              change, rejected ("close the drawer"). Idle / terminal: no change.
    // Effects:     CANCELLED outcome record; UNEXPECTED_EVENT for the AwaitingClosure rejection.
    OpResult cancel();

    // tick()
    // Reqs:        FR-DISP-004, FR-DISP-007, FR-FAULT-004, FR-INV-002, NFR-PRF-002, NFR-REL-003
    // Pre:         called every tickPeriod.
    // Post:        INV-DSP-4: each change of the sensed count of the drawer under control has been
    //              recorded (recordRemoval / recordAddition) and fired as SM2 medicationRemoved;
    //              an open->closed edge has fired compartmentClosed (closure: function table
    //              DSP-CLOSE); expired timers have fired retrievalTimeout / closureTimeout; a
    //              drawer of a finished session is released once it is closed.
    // Effects:     inventory changes; outcome records; IncompleteDispense / InventoryUpdateFailed
    //              faults (C5); lock commands; session persisted.
    void tick();

    // onFaultDetected(fault)   (FaultDetector listener)
    // Reqs:        FR-FAULT-001, FR-MODE-005, NFR-SAF-001, NFR-AVL-004
    // Post:        critical fault and an active session: state() == Interrupted, the drawer is
    //              commanded LOCKED at once (it engages when closed) and stays under control until
    //              closed, so later removals are still recorded. Non-critical faults change nothing.
    void onFaultDetected(const Fault& fault);

    // recoverInterruptedSession()
    // Reqs:        NFR-SAF-003, US-SAF-01 AC-03
    // Pre:         called once at start-up, before the first tick.
    // Post:        if a session was active when the ADC stopped, it is now Interrupted with exactly
    //              one outcome record, its drawer is commanded LOCKED and under control until
    //              closed; it can never resume. Otherwise nothing changes.
    void recoverInterruptedSession();

    // state() -> current SM2 state (of the current or last session).
    DispenseState state() const;

    // sessionActive() -> true in AuthorizationPending, AwaitingRetrieval, AwaitingClosure.
    bool sessionActive() const;

    // busy() -> sessionActive() or a drawer is still under control (holds the session timer, C2).
    bool busy() const;

    // drawersUnderControl() -> drawers FaultDetector must treat as authorized (empty or one drawer).
    std::set<DrawerId> drawersUnderControl() const;

    // session() -> copy of the current / last session for display.
    SessionInfo session() const;

    // transitionFor(state, event) -> DispenseCell
    // Reqs:        D1 State Machine 2 (documentation / test of the table itself)
    // Post:        the cell [state][event] of the SM2 table. Pure.
    static DispenseCell transitionFor(DispenseState state, DispenseEvent event);

    // addOutcomeListener(listener): called once per finished session (terminal state reached).
    void addOutcomeListener(OutcomeListener listener);

private:
    using Guard = std::function<bool()>;
    using Otherwise = std::function<void()>;

    OpResult fire(DispenseEvent event, const std::string& reason, const Guard& guard = nullptr,
                  const Otherwise& otherwise = nullptr);
    void enter(DispenseState state, const std::string& reason);
    void beginSession(SessionKind kind, const MedicationId& medication, int quantity);
    OpResult rejectAttempt(SessionKind kind, const MedicationId& medication, int quantity, ResultCode code,
                           const std::string& reason);
    OpResult evaluateRequest(SessionKind kind, const MedicationId& medication, int quantity);
    bool drawerReady(const DrawerId& d) const;
    void failSafe(DispenseEvent event);
    void finish(const std::string& reason);
    void persist();
    void trackCounts();
    void watchUntrackedDrawers();

    AccessControlManager& access_;
    InventoryManager& inventory_;
    OperationalModeManager& modes_;
    FaultDetector& faults_;
    HardwareInterface& hardware_;
    EventLogger& logger_;
    Storage& storage_;
    const Clock& clock_;
    AdcConfig config_;

    DispenseState state_ = DispenseState::Idle;
    DispenseState previous_ = DispenseState::Idle;  // state before the last transition
    bool failSafeTripped_ = false;
    // Current / last session (SM2 data).
    SessionKind kind_ = SessionKind::Dispense;
    Badge user_;
    MedicationId medication_;
    DrawerId drawer_;
    int requested_ = 0;
    int moved_ = 0;
    bool unlocked_ = false;  // this session commanded its drawer UNLOCKED
    std::chrono::milliseconds deadline_{0};
    // Drawer under control (outlives a session until the drawer closes).
    bool tracking_ = false;
    DrawerId trackedDrawer_;
    MedicationId trackedMedication_;
    Badge trackedUser_;
    int lastRecordedCount_ = 0;
    bool trackedWasOpen_ = false;
    // Last sensed count of drawers not under control (to see unexpected removals in Idle).
    std::map<DrawerId, int> lastSeen_;
    std::vector<OutcomeListener> listeners_;
};

}  // namespace adc
