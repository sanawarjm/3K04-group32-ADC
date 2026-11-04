#include "adc/operational_mode_manager.hpp"

#include <utility>

namespace adc {

namespace {

const char* const kSystem = "ADC";

// ---- SM1 transition table (D1 State Machine 1; brief section 2.2) ----------------------------
// Every cell is a decision:
//   Go       -> change to `target` (entry action of target runs)
//   Guarded  -> evaluate the D1 guard; true: change to `target`; false: stay, the guard's own
//               denial is logged and returned (rejectRequest/logAttempt, "log denial")
//   Stay     -> internal action, no state change
//   Ignore   -> no change, no log (D1 "/ ignore" or harmless)
//   Reject   -> no change + UNEXPECTED_EVENT log + rejection message (user caused it)
//   Illegal  -> cannot happen if the table is right: failSafe()
enum class Kind { Go, Guarded, Stay, Ignore, Reject, Illegal };

struct Cell {
    Kind kind;
    Mode target;
    const char* actions;  // D1 action names, for the log and the documentation
};

constexpr int kModeCount = 5;   // Startup, Normal, Maintenance, Fault, Off
constexpr int kEventCount = 7;  // see ModeEvent

// columns: startupChecksPassed, startupCheckFailed, enterMaintenance, exitMaintenance,
//          faultDetected, faultCleared, powerOff
const Cell kTable[kModeCount][kEventCount] = {
    /* Startup */ {
        {Kind::Go, Mode::Normal, "enterNormalMode, logModeChange"},
        {Kind::Go, Mode::Fault, "disableDispensing, displayFault, logFault"},
        {Kind::Reject, Mode::Startup, "reject + log"},
        {Kind::Reject, Mode::Startup, "reject + log"},
        {Kind::Go, Mode::Fault, "disableDispensing, displayFault, logFault"},
        {Kind::Ignore, Mode::Startup, "ignore"},
        {Kind::Go, Mode::Off, "shutdown"},
    },
    /* Normal */ {
        {Kind::Ignore, Mode::Normal, "ignore"},
        {Kind::Ignore, Mode::Normal, "ignore"},
        {Kind::Guarded, Mode::Maintenance, "[authorizedForMaintenance] restrictOperations, logModeChange"},
        {Kind::Reject, Mode::Normal, "reject + log"},
        {Kind::Go, Mode::Fault, "disableDispensing, displayFault, logFault"},
        {Kind::Ignore, Mode::Normal, "ignore"},
        {Kind::Go, Mode::Off, "shutdown"},
    },
    /* Maintenance */ {
        {Kind::Ignore, Mode::Maintenance, "ignore"},
        {Kind::Ignore, Mode::Maintenance, "ignore"},
        {Kind::Ignore, Mode::Maintenance, "D1: enterMaintenance / ignore"},
        {Kind::Guarded, Mode::Normal, "[authorizedForMaintenance] resumeNormalOperation, logModeChange"},
        {Kind::Go, Mode::Fault, "disableDispensing, displayFault, logFault"},
        {Kind::Ignore, Mode::Maintenance, "ignore"},
        {Kind::Go, Mode::Off, "shutdown"},
    },
    /* Fault */ {
        {Kind::Ignore, Mode::Fault, "ignore"},
        {Kind::Ignore, Mode::Fault, "ignore"},
        {Kind::Reject, Mode::Fault, "reject + log"},
        {Kind::Reject, Mode::Fault, "reject + log"},
        {Kind::Stay, Mode::Fault, "D1: faultDetected / ignore; C6: fault still logged by FaultDetector"},
        {Kind::Guarded, Mode::Normal, "[authorizedUser and all critical faults gone] resumeNormalOperation, logModeChange"},
        {Kind::Go, Mode::Off, "shutdown"},
    },
    /* Off (D1 final state) */ {
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Illegal, Mode::Off, "illegal"},
        {Kind::Ignore, Mode::Off, "ignore"},
    },
};

std::string actionText(ModeEvent event) {
    switch (event) {
        case ModeEvent::EnterMaintenance: return "Entering maintenance mode";
        case ModeEvent::ExitMaintenance: return "Leaving maintenance mode";
        case ModeEvent::FaultCleared: return "Clearing a fault";
        default: return std::string("Event ") + toString(event);
    }
}

std::string ignoredText(ModeEvent event, Mode mode) {
    switch (event) {
        case ModeEvent::EnterMaintenance: return "The cabinet is already in MAINTENANCE mode.";
        case ModeEvent::FaultCleared: return "There is no active fault to clear.";
        default: return actionText(event) + " has no effect in " + toString(mode) + " mode.";
    }
}

}  // namespace

OperationalModeManager::OperationalModeManager(AccessControlManager& access, FaultDetector& faults,
                                               EventLogger& logger, HardwareInterface& hardware)
    : access_(access), faults_(faults), logger_(logger), hardware_(hardware) {}

Mode OperationalModeManager::mode() const { return mode_; }

bool OperationalModeManager::dispensingAllowed() const {
    return mode_ == Mode::Normal && !faults_.hasCriticalFault() && !failSafeTripped_;
}

bool OperationalModeManager::replenishmentAllowed() const {
    return (mode_ == Mode::Normal || mode_ == Mode::Maintenance) && !faults_.hasCriticalFault() && !failSafeTripped_;
}

std::string OperationalModeManager::actor() const {
    return access_.isAuthenticated() ? access_.currentUser() : std::string(kSystem);
}

OpResult OperationalModeManager::fire(ModeEvent event, const std::string& reason, const Guard& guard) {
    const int row = static_cast<int>(mode_);
    const int col = static_cast<int>(event);
    if (row < 0 || row >= kModeCount || col < 0 || col >= kEventCount) {
        failSafe(event);
        return OpResult::failure(ResultCode::InvalidEvent, "Internal error: dispensing disabled.");
    }
    const Cell& cell = kTable[row][col];
    switch (cell.kind) {
        case Kind::Go:
            changeMode(cell.target, actor(), reason);
            return OpResult::success(std::string("Mode is now ") + toString(mode_) + ".");
        case Kind::Guarded: {
            const OpResult allowed = guard ? guard() : OpResult::success("");
            if (!allowed.ok()) return allowed;  // stay; denial already logged by the guard
            changeMode(cell.target, actor(), reason);
            return OpResult::success(std::string("Mode is now ") + toString(mode_) + ".");
        }
        case Kind::Stay:
            return OpResult::success("");
        case Kind::Ignore:
            return OpResult::failure(ResultCode::InvalidEvent, ignoredText(event, mode_));
        case Kind::Reject:
            logger_.record(EventType::UNEXPECTED_EVENT, actor(), "REJECTED",
                           std::string("SM1 mode=") + toString(mode_) + " event=" + toString(event));
            return OpResult::failure(ResultCode::InvalidEvent,
                                     actionText(event) + " is not available in " + toString(mode_) + " mode.");
        case Kind::Illegal:
            failSafe(event);
            return OpResult::failure(ResultCode::InvalidEvent, "Internal error: dispensing disabled.");
    }
    return OpResult::failure(ResultCode::InvalidEvent, "");
}

void OperationalModeManager::changeMode(Mode to, const std::string& who, const std::string& reason) {
    const Mode from = mode_;
    mode_ = to;
    logger_.record(EventType::MODE_CHANGED, who, toString(to),
                   std::string(toString(from)) + "->" + toString(to) + " reason=" + reason);  // INV-MODE-3
    enter(to);
    for (const ModeListener& listener : modeListeners_) listener(from, to, reason);
}

// Entry actions: one place per state.
void OperationalModeManager::enter(Mode state) {
    switch (state) {
        case Mode::Normal:       // enterNormalMode / resumeNormalOperation
        case Mode::Maintenance:  // restrictOperations
        case Mode::Fault:        // disableDispensing (dispensingAllowed() is now false), displayFault
        case Mode::Startup:
            refreshIndicator();
            break;
        case Mode::Off:  // shutdown
            logger_.record(EventType::SYSTEM_SHUTDOWN, kSystem, "OFF", "cabinet powered off");
            break;
    }
}

void OperationalModeManager::failSafe(ModeEvent event) {
    failSafeTripped_ = true;
    logger_.record(EventType::UNEXPECTED_EVENT, kSystem, "ILLEGAL",
                   std::string("SM1 mode=") + toString(mode_) + " event=" + toString(event) + " fail-safe tripped");
    hardware_.o_setIndicator(IndicatorLevel::Fault, "Internal error: dispensing disabled. Call a technician.");
}

void OperationalModeManager::completeStartup(const std::vector<Fault>& selfTestFaults) {
    if (selfTestFaults.empty() && !faults_.hasCriticalFault()) {
        fire(ModeEvent::StartupChecksPassed, "start-up checks passed", nullptr);
    } else {
        fire(ModeEvent::StartupCheckFailed, "start-up check failed", nullptr);
    }
}

OpResult OperationalModeManager::requestEnterMaintenance() {
    const OpResult r = fire(ModeEvent::EnterMaintenance, "maintenance requested", [this] {
        return access_.authorize(Permission::EnterMaintenance, "enter maintenance mode");
    });
    if (r.ok() && mode_ == Mode::Maintenance) {
        logger_.record(EventType::MAINTENANCE_ACTIVITY, actor(), "STARTED", "entered maintenance mode");
        for (const MaintenanceListener& l : maintenanceListeners_) l("entered maintenance mode", actor());
        return OpResult::success("Maintenance mode started. Dispensing is paused.");
    }
    return r;
}

OpResult OperationalModeManager::requestExitMaintenance() {
    const OpResult r = fire(ModeEvent::ExitMaintenance, "maintenance finished", [this] {
        return access_.authorize(Permission::ExitMaintenance, "leave maintenance mode");
    });
    if (r.ok() && mode_ == Mode::Normal) {
        logger_.record(EventType::MAINTENANCE_ACTIVITY, actor(), "FINISHED", "left maintenance mode");
        for (const MaintenanceListener& l : maintenanceListeners_) l("left maintenance mode", actor());
        return OpResult::success("Maintenance finished. The cabinet is back in NORMAL mode.");
    }
    return r;
}

OpResult OperationalModeManager::runDiagnostics(const std::map<DrawerId, int>& expectedCounts) {
    // MODE-DIAG R1
    const OpResult allowed = access_.authorize(Permission::RunDiagnostics, "run diagnostics");
    if (!allowed.ok()) return allowed;
    // R2
    if (mode_ != Mode::Maintenance && mode_ != Mode::Fault)
        return OpResult::failure(ResultCode::ModeNotAllowed,
                                 std::string("Diagnostics can only be run in MAINTENANCE or FAULT mode (now ") +
                                     toString(mode_) + ").");
    // R3
    const std::vector<std::string> findings = faults_.diagnose(expectedCounts);
    std::string summary = findings.empty() ? "all drawer checks passed" : "";
    for (const std::string& f : findings) summary += (summary.empty() ? "" : " ") + f;
    logger_.record(EventType::MAINTENANCE_ACTIVITY, actor(), findings.empty() ? "PASSED" : "PROBLEMS_FOUND",
                   "diagnostics: " + summary);
    for (const MaintenanceListener& l : maintenanceListeners_) l("diagnostics: " + summary, actor());
    if (findings.empty()) return OpResult::success("Diagnostics: all drawer checks passed.");
    return OpResult::failure(ResultCode::FaultStillPresent, "Diagnostics found problems: " + summary);
}

OpResult OperationalModeManager::requestClearFault(const std::map<DrawerId, int>& expectedCounts) {
    const OpResult r = fire(ModeEvent::FaultCleared, "fault cleared by technician", [this, &expectedCounts] {
        const OpResult allowed = access_.authorize(Permission::ClearFault, "clear a fault");
        if (!allowed.ok()) return allowed;
        return faults_.tryClear(access_.currentUser(), expectedCounts);
    });
    if (r.ok()) return OpResult::success("Fault cleared. The cabinet is back in NORMAL mode.");
    return r;
}

void OperationalModeManager::onFaultDetected(const Fault& fault) {
    if (!fault.critical) {
        refreshIndicator();  // CommunicationLoss: WARNING only, mode unchanged (US-FAULT-01 AC-03)
        return;
    }
    const Mode before = mode_;
    fire(ModeEvent::FaultDetected,
         std::string("fault ") + toString(fault.type) + (fault.drawer.empty() ? "" : " drawer=" + fault.drawer), nullptr);
    if (before == Mode::Fault) refreshIndicator();  // Stay cell: show the newest fault too
}

void OperationalModeManager::powerOff() { fire(ModeEvent::PowerOff, "power off", nullptr); }

void OperationalModeManager::refreshIndicator() {
    if (failSafeTripped_) {
        hardware_.o_setIndicator(IndicatorLevel::Fault, "Internal error: dispensing disabled. Call a technician.");
        return;
    }
    switch (mode_) {
        case Mode::Fault: {
            std::string text = "Dispensing is stopped. Call a technician.";
            for (const Fault& f : faults_.activeFaults()) {
                if (f.critical) {
                    text = f.message;
                    break;
                }
            }
            hardware_.o_setIndicator(IndicatorLevel::Fault, text);
            return;
        }
        case Mode::Maintenance:
            hardware_.o_setIndicator(IndicatorLevel::Warning, "Maintenance mode: dispensing is paused.");
            return;
        case Mode::Startup:
            hardware_.o_setIndicator(IndicatorLevel::Warning, "Starting up: checking the hardware.");
            return;
        case Mode::Normal:
            for (const Fault& f : faults_.activeFaults()) {
                if (!f.critical) {
                    hardware_.o_setIndicator(IndicatorLevel::Warning, f.message);
                    return;
                }
            }
            hardware_.o_setIndicator(IndicatorLevel::Ok, "Ready.");
            return;
        case Mode::Off:
            hardware_.o_setIndicator(IndicatorLevel::Warning, "Cabinet is off.");
            return;
    }
}

void OperationalModeManager::addModeListener(ModeListener listener) { modeListeners_.push_back(std::move(listener)); }

void OperationalModeManager::addMaintenanceListener(MaintenanceListener listener) {
    maintenanceListeners_.push_back(std::move(listener));
}

}  // namespace adc
