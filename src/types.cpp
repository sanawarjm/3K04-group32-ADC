#include "adc/types.hpp"

#include <cstddef>
#include <utility>

namespace adc {

namespace {

// One name table per enum. The order does not matter; each enumerator appears once.
template <typename E, std::size_t N>
const char* nameOf(const std::pair<E, const char*> (&table)[N], E value) {
    for (const auto& entry : table) {
        if (entry.first == value) return entry.second;
    }
    return "UNKNOWN";
}

template <typename E, std::size_t N>
bool valueOf(const std::pair<E, const char*> (&table)[N], const std::string& text, E& out) {
    for (const auto& entry : table) {
        if (text == entry.second) {
            out = entry.first;
            return true;
        }
    }
    return false;
}

const std::pair<Role, const char*> kRoles[] = {
    {Role::Nurse, "Nurse"}, {Role::Pharmacist, "Pharmacist"}, {Role::Technician, "Technician"}};

const std::pair<Permission, const char*> kPermissions[] = {
    {Permission::Dispense, "Dispense"},
    {Permission::ViewInventory, "ViewInventory"},
    {Permission::Replenish, "Replenish"},
    {Permission::ViewLog, "ViewLog"},
    {Permission::EnterMaintenance, "EnterMaintenance"},
    {Permission::ExitMaintenance, "ExitMaintenance"},
    {Permission::RunDiagnostics, "RunDiagnostics"},
    {Permission::ClearFault, "ClearFault"},
    {Permission::ModifyAuditRecords, "ModifyAuditRecords"}};

const std::pair<Mode, const char*> kModes[] = {{Mode::Startup, "STARTUP"},
                                               {Mode::Normal, "NORMAL"},
                                               {Mode::Maintenance, "MAINTENANCE"},
                                               {Mode::Fault, "FAULT"},
                                               {Mode::Off, "OFF"}};

const std::pair<ModeEvent, const char*> kModeEvents[] = {
    {ModeEvent::StartupChecksPassed, "startupChecksPassed"},
    {ModeEvent::StartupCheckFailed, "startupCheckFailed"},
    {ModeEvent::EnterMaintenance, "enterMaintenance"},
    {ModeEvent::ExitMaintenance, "exitMaintenance"},
    {ModeEvent::FaultDetected, "faultDetected"},
    {ModeEvent::FaultCleared, "faultCleared"},
    {ModeEvent::PowerOff, "powerOff"}};

const std::pair<DispenseState, const char*> kDispenseStates[] = {
    {DispenseState::Idle, "IDLE"},
    {DispenseState::AuthorizationPending, "AUTHORIZATION_PENDING"},
    {DispenseState::AwaitingRetrieval, "AWAITING_RETRIEVAL"},
    {DispenseState::AwaitingClosure, "AWAITING_CLOSURE"},
    {DispenseState::Rejected, "REJECTED"},
    {DispenseState::Cancelled, "CANCELLED"},
    {DispenseState::TimedOut, "TIMED_OUT"},
    {DispenseState::Completed, "COMPLETED"},
    {DispenseState::Interrupted, "INTERRUPTED"}};

const std::pair<DispenseEvent, const char*> kDispenseEvents[] = {
    {DispenseEvent::DispenseRequest, "dispenseRequest"},
    {DispenseEvent::AuthorizationApproved, "authorizationApproved"},
    {DispenseEvent::AuthorizationDenied, "authorizationDenied"},
    {DispenseEvent::MedicationRemoved, "medicationRemoved"},
    {DispenseEvent::CancelRequest, "cancelRequest"},
    {DispenseEvent::RetrievalTimeout, "retrievalTimeout"},
    {DispenseEvent::CompartmentClosed, "compartmentClosed"},
    {DispenseEvent::ClosureTimeout, "closureTimeout"},
    {DispenseEvent::FaultDetected, "faultDetected"}};

const std::pair<LinkState, const char*> kLinkStates[] = {
    {LinkState::Connecting, "CONNECTING"},
    {LinkState::Connected, "CONNECTED"},
    {LinkState::Disconnected, "DISCONNECTED"},
    {LinkState::Resynchronizing, "RESYNCHRONIZING"},
    {LinkState::Off, "OFF"}};

const std::pair<LinkEvent, const char*> kLinkEvents[] = {
    {LinkEvent::ConnectionEstablished, "connectionEstablished"},
    {LinkEvent::CommunicationLost, "communicationLost"},
    {LinkEvent::ConnectionRestored, "connectionRestored"},
    {LinkEvent::SynchronizationComplete, "synchronizationComplete"},
    {LinkEvent::OutgoingInformation, "outgoingInformation"},
    {LinkEvent::PowerOff, "powerOff"}};

const std::pair<FaultType, const char*> kFaultTypes[] = {
    {FaultType::StorageNotClosed, "StorageNotClosed"},
    {FaultType::SensorFailure, "SensorFailure"},
    {FaultType::IncompleteDispense, "IncompleteDispense"},
    {FaultType::InventoryUpdateFailed, "InventoryUpdateFailed"},
    {FaultType::StorageFailure, "StorageFailure"},
    {FaultType::StartupCheckFailed, "StartupCheckFailed"},
    {FaultType::UnauthorizedAccess, "UnauthorizedAccess"},
    {FaultType::CommunicationLoss, "CommunicationLoss"}};

const std::pair<EventType, const char*> kEventTypes[] = {
    {EventType::SYSTEM_STARTUP, "SYSTEM_STARTUP"},
    {EventType::SYSTEM_SHUTDOWN, "SYSTEM_SHUTDOWN"},
    {EventType::AUTH_SUCCESS, "AUTH_SUCCESS"},
    {EventType::AUTH_FAILURE, "AUTH_FAILURE"},
    {EventType::SESSION_ENDED, "SESSION_ENDED"},
    {EventType::ACCESS_DENIED, "ACCESS_DENIED"},
    {EventType::DISPENSE_OUTCOME, "DISPENSE_OUTCOME"},
    {EventType::REPLENISH_OUTCOME, "REPLENISH_OUTCOME"},
    {EventType::INVENTORY_CHANGED, "INVENTORY_CHANGED"},
    {EventType::FAULT_DETECTED, "FAULT_DETECTED"},
    {EventType::FAULT_CLEARED, "FAULT_CLEARED"},
    {EventType::FAULT_CLEAR_DENIED, "FAULT_CLEAR_DENIED"},
    {EventType::MODE_CHANGED, "MODE_CHANGED"},
    {EventType::MAINTENANCE_ACTIVITY, "MAINTENANCE_ACTIVITY"},
    {EventType::COMM_STATE_CHANGED, "COMM_STATE_CHANGED"},
    {EventType::UNEXPECTED_EVENT, "UNEXPECTED_EVENT"},
    {EventType::CONFIG_UPDATED, "CONFIG_UPDATED"}};

const std::pair<ResultCode, const char*> kResultCodes[] = {
    {ResultCode::Ok, "Ok"},
    {ResultCode::AuthFailed, "AuthFailed"},
    {ResultCode::NotAuthenticated, "NotAuthenticated"},
    {ResultCode::PermissionDenied, "PermissionDenied"},
    {ResultCode::SessionActive, "SessionActive"},
    {ResultCode::UnknownMedication, "UnknownMedication"},
    {ResultCode::InvalidQuantity, "InvalidQuantity"},
    {ResultCode::ModeNotAllowed, "ModeNotAllowed"},
    {ResultCode::InsufficientStock, "InsufficientStock"},
    {ResultCode::DrawerNotReady, "DrawerNotReady"},
    {ResultCode::StorageError, "StorageError"},
    {ResultCode::InvalidEvent, "InvalidEvent"},
    {ResultCode::FaultStillPresent, "FaultStillPresent"},
    {ResultCode::InvalidInput, "InvalidInput"}};

const std::pair<LockCommand, const char*> kLockCommands[] = {{LockCommand::Locked, "LOCKED"},
                                                              {LockCommand::Unlocked, "UNLOCKED"}};

const std::pair<IndicatorLevel, const char*> kIndicatorLevels[] = {
    {IndicatorLevel::Ok, "OK"}, {IndicatorLevel::Warning, "WARNING"}, {IndicatorLevel::Fault, "FAULT"}};

}  // namespace

const char* toString(Role v) { return nameOf(kRoles, v); }
const char* toString(Permission v) { return nameOf(kPermissions, v); }
const char* toString(Mode v) { return nameOf(kModes, v); }
const char* toString(ModeEvent v) { return nameOf(kModeEvents, v); }
const char* toString(DispenseState v) { return nameOf(kDispenseStates, v); }
const char* toString(DispenseEvent v) { return nameOf(kDispenseEvents, v); }
const char* toString(LinkState v) { return nameOf(kLinkStates, v); }
const char* toString(LinkEvent v) { return nameOf(kLinkEvents, v); }
const char* toString(FaultType v) { return nameOf(kFaultTypes, v); }
const char* toString(EventType v) { return nameOf(kEventTypes, v); }
const char* toString(ResultCode v) { return nameOf(kResultCodes, v); }
const char* toString(LockCommand v) { return nameOf(kLockCommands, v); }
const char* toString(IndicatorLevel v) { return nameOf(kIndicatorLevels, v); }

bool fromString(const std::string& t, Role& o) { return valueOf(kRoles, t, o); }
bool fromString(const std::string& t, Permission& o) { return valueOf(kPermissions, t, o); }
bool fromString(const std::string& t, Mode& o) { return valueOf(kModes, t, o); }
bool fromString(const std::string& t, ModeEvent& o) { return valueOf(kModeEvents, t, o); }
bool fromString(const std::string& t, DispenseState& o) { return valueOf(kDispenseStates, t, o); }
bool fromString(const std::string& t, DispenseEvent& o) { return valueOf(kDispenseEvents, t, o); }
bool fromString(const std::string& t, LinkState& o) { return valueOf(kLinkStates, t, o); }
bool fromString(const std::string& t, LinkEvent& o) { return valueOf(kLinkEvents, t, o); }
bool fromString(const std::string& t, FaultType& o) { return valueOf(kFaultTypes, t, o); }
bool fromString(const std::string& t, EventType& o) { return valueOf(kEventTypes, t, o); }
bool fromString(const std::string& t, ResultCode& o) { return valueOf(kResultCodes, t, o); }
bool fromString(const std::string& t, LockCommand& o) { return valueOf(kLockCommands, t, o); }
bool fromString(const std::string& t, IndicatorLevel& o) { return valueOf(kIndicatorLevels, t, o); }

bool isTerminal(DispenseState state) {
    switch (state) {
        case DispenseState::Rejected:
        case DispenseState::Cancelled:
        case DispenseState::TimedOut:
        case DispenseState::Completed:
        case DispenseState::Interrupted:
            return true;
        default:
            return false;
    }
}

}  // namespace adc
