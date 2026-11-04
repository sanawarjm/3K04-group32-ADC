#pragma once
// Shared vocabulary of the ADC (not a D1 component; added for D2).
// Every enum below uses D1's names: roles from D1 section 7, states and events from the three
// D1 state machines, fault situations from the D1 fault user stories and activity diagrams.
//
// ---- Module Interface Specification (MG 1.3.5) --------------------------------
// Responsibility: names the values every module exchanges (roles, permissions, modes, states,
//                 events, faults, results) and converts each enum to and from text.
// Secret:         the text spelling of each value (used in the log file, messages and screen).
// Uses:           nothing (C++ standard library only).
// State invariant(s): none (no state).

#include <cstdint>
#include <string>

namespace adc {

using Badge = std::string;         // user identifier shown on the badge, e.g. "N100"
using MedicationId = std::string;  // e.g. "M03"
using DrawerId = std::string;      // e.g. "D3"

// D1 section 7: one role per user.
enum class Role { Nurse, Pharmacist, Technician };

// Protected actions (D1 use case diagram). The role -> permission table is the secret of
// AccessControlManager.
enum class Permission {
    Dispense,
    ViewInventory,
    Replenish,
    ViewLog,
    EnterMaintenance,
    ExitMaintenance,
    RunDiagnostics,
    ClearFault,
    ModifyAuditRecords  // NFR-AUD-005: granted to nobody
};

// D1 State Machine 1 states. Off is the D1 final pseudo-state reached by powerOff.
enum class Mode { Startup, Normal, Maintenance, Fault, Off };

// D1 State Machine 1 events.
enum class ModeEvent {
    StartupChecksPassed,
    StartupCheckFailed,
    EnterMaintenance,
    ExitMaintenance,
    FaultDetected,
    FaultCleared,
    PowerOff
};

// D1 State Machine 2 states. Rejected .. Interrupted are terminal (FR-DISP-006, NFR-SAF-003).
enum class DispenseState {
    Idle,
    AuthorizationPending,
    AwaitingRetrieval,
    AwaitingClosure,
    Rejected,
    Cancelled,
    TimedOut,
    Completed,
    Interrupted
};

// D1 State Machine 2 events.
enum class DispenseEvent {
    DispenseRequest,
    AuthorizationApproved,
    AuthorizationDenied,
    MedicationRemoved,
    CancelRequest,
    RetrievalTimeout,
    CompartmentClosed,
    ClosureTimeout,
    FaultDetected
};

// D1 State Machine 3 states. Off is the D1 final pseudo-state reached by powerOff.
enum class LinkState { Connecting, Connected, Disconnected, Resynchronizing, Off };

// D1 State Machine 3 events.
enum class LinkEvent {
    ConnectionEstablished,
    CommunicationLost,
    ConnectionRestored,
    SynchronizationComplete,
    OutgoingInformation,
    PowerOff
};

// D1 fault situations (US-FAULT-01, FR-FAULT-001..004, NFR-REL-001/002/004).
enum class FaultType {
    StorageNotClosed,       // US-FAULT-01 AC-02: storage location does not close correctly
    SensorFailure,          // a sensor gives no reading
    IncompleteDispense,     // FR-FAULT-004 / FR-DISP-007
    InventoryUpdateFailed,  // US-INV-01 AC-03: inventory fault
    StorageFailure,         // NFR-REL-001/002: audit records cannot be saved
    StartupCheckFailed,     // NFR-REL-004
    UnauthorizedAccess,     // drawer opened without an authorized session
    CommunicationLoss       // FR-FAULT-003: NOT critical (US-FAULT-01 AC-03)
};

// Audit record types (FR-LOG-*, NFR-AUD-*).
enum class EventType {
    SYSTEM_STARTUP,
    SYSTEM_SHUTDOWN,
    AUTH_SUCCESS,
    AUTH_FAILURE,
    SESSION_ENDED,
    ACCESS_DENIED,
    DISPENSE_OUTCOME,
    REPLENISH_OUTCOME,
    INVENTORY_CHANGED,
    FAULT_DETECTED,
    FAULT_CLEARED,
    FAULT_CLEAR_DENIED,
    MODE_CHANGED,
    MAINTENANCE_ACTIVITY,
    COMM_STATE_CHANGED,
    UNEXPECTED_EVENT,
    CONFIG_UPDATED
};

// Outcome of an operation a user asked for. User mistakes are reported with these, never thrown.
enum class ResultCode {
    Ok,
    AuthFailed,
    NotAuthenticated,
    PermissionDenied,
    SessionActive,
    UnknownMedication,
    InvalidQuantity,
    ModeNotAllowed,
    InsufficientStock,
    DrawerNotReady,
    StorageError,
    InvalidEvent,
    FaultStillPresent,
    InvalidInput
};

enum class LockCommand { Locked, Unlocked };

// NFR-USB-003: every level is shown as a text label plus a light pattern (plus buzzer for FAULT).
enum class IndicatorLevel { Ok, Warning, Fault };

// Result of a user-visible operation. `message` is plain language and safe to display
// (NFR-USB-001, NFR-USB-002). A value record, not a module.
struct OpResult {
    ResultCode code = ResultCode::Ok;
    std::string message;

    bool ok() const { return code == ResultCode::Ok; }
    static OpResult success(const std::string& text) { return OpResult{ResultCode::Ok, text}; }
    static OpResult failure(ResultCode c, const std::string& text) { return OpResult{c, text}; }
};

// One medication record (FR-INV-001, FR-DISP-002). A value record.
struct Medication {
    MedicationId id;
    std::string name;
    DrawerId drawer;
    int quantity = 0;
};

// One active fault (FR-FAULT-002). `message` is the plain-language text for the screen
// (NFR-USB-002); `detail` is technical text for the log. A value record.
struct Fault {
    FaultType type = FaultType::SensorFailure;
    DrawerId drawer;  // empty when the fault is not about one drawer
    std::string detail;
    std::string message;
    bool critical = true;
};

// One audit record (FR-LOG-001..003, NFR-AUD-002). Never holds a PIN or patient data.
struct EventRecord {
    std::int64_t id = 0;
    std::string timestamp;
    EventType type = EventType::SYSTEM_STARTUP;
    std::string user;        // badge, or "ADC" for the system
    std::string medication;  // empty when not about a medication
    int quantity = 0;
    std::string outcome;
    std::string details;
};

// ---- text conversions ---------------------------------------------------------
// toString(x) -> text
// Reqs:        NFR-USB-004 (plain wording), NFR-AUD-003 (stable log format)
// Pre:         x is a declared enumerator.
// Post:        returns the unique, fixed spelling of x; fromString(toString(x), y) sets y == x.
// Effects:     none.
// Exceptions:  none.
const char* toString(Role value);
const char* toString(Permission value);
const char* toString(Mode value);
const char* toString(ModeEvent value);
const char* toString(DispenseState value);
const char* toString(DispenseEvent value);
const char* toString(LinkState value);
const char* toString(LinkEvent value);
const char* toString(FaultType value);
const char* toString(EventType value);
const char* toString(ResultCode value);
const char* toString(LockCommand value);
const char* toString(IndicatorLevel value);

// fromString(text, out) -> bool
// Pre:         none.
// Post:        true and out set when text is the spelling of an enumerator; false and out
//              unchanged otherwise.
// Effects:     none.
// Exceptions:  none.
bool fromString(const std::string& text, Role& out);
bool fromString(const std::string& text, Permission& out);
bool fromString(const std::string& text, Mode& out);
bool fromString(const std::string& text, ModeEvent& out);
bool fromString(const std::string& text, DispenseState& out);
bool fromString(const std::string& text, DispenseEvent& out);
bool fromString(const std::string& text, LinkState& out);
bool fromString(const std::string& text, LinkEvent& out);
bool fromString(const std::string& text, FaultType& out);
bool fromString(const std::string& text, EventType& out);
bool fromString(const std::string& text, ResultCode& out);
bool fromString(const std::string& text, LockCommand& out);
bool fromString(const std::string& text, IndicatorLevel& out);

// isTerminal(state) -> bool
// Reqs:        FR-DISP-006, NFR-SAF-003
// Post:        true exactly for Rejected, Cancelled, TimedOut, Completed, Interrupted.
bool isTerminal(DispenseState state);

}  // namespace adc
