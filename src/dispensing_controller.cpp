#include "adc/dispensing_controller.hpp"

#include <array>
#include <charconv>
#include <system_error>
#include <utility>

namespace adc {

namespace {

const char* const kSessionFile = "session";
constexpr std::size_t kSessionFields = 8;  // kind, user, medication, drawer, requested, moved, state, unlocked
const char* const kDispenseKind = "Dispense";
const char* const kReplenishKind = "Replenish";
const char* const kSystem = "ADC";
constexpr int kMillisPerSecond = 1000;  // unit conversion for messages

// ---- SM2 transition table (D1 State Machine 2; brief section 2.2) ----------------------------
// Kinds as in SM1: Go, Guarded (D1 guard; `otherwise` runs when false), Stay (internal action
// done by the caller), Ignore (no change, no log), Reject (no change + UNEXPECTED_EVENT +
// message), Illegal (failSafe).
enum class Kind { Go, Guarded, Stay, Ignore, Reject, Illegal };

struct Cell {
    Kind kind;
    DispenseState target;
    const char* actions;  // D1 action names
};

constexpr int kStateCount = 9;  // see DispenseState
constexpr int kEventCount = 9;  // see DispenseEvent

using S = DispenseState;
using Row = std::array<Cell, kEventCount>;
constexpr Cell ignoreCell(S s) { return Cell{Kind::Ignore, s, "ignore"}; }
constexpr Cell unexpectedCell(S s) { return Cell{Kind::Reject, s, "logUnexpectedEvent"}; }
constexpr Cell kNewSession{Kind::Go, S::AuthorizationPending, "beginSession"};
constexpr Cell kInterrupt{Kind::Go, S::Interrupted, "disableDispensing, lockCompartment, logFault"};

// The five terminal rows are identical: only a NEW request leaves them (FR-DISP-006, NFR-SAF-003).
constexpr Row terminalRow(S s) {
    return Row{kNewSession,    ignoreCell(s), ignoreCell(s), unexpectedCell(s), ignoreCell(s),
               ignoreCell(s), ignoreCell(s), ignoreCell(s), ignoreCell(s)};
}

// columns: dispenseRequest, authorizationApproved, authorizationDenied, medicationRemoved,
//          cancelRequest, retrievalTimeout, compartmentClosed, closureTimeout, faultDetected
const std::array<Row, kStateCount> kTable{{
    /* Idle */ Row{kNewSession, ignoreCell(S::Idle), ignoreCell(S::Idle), unexpectedCell(S::Idle), ignoreCell(S::Idle),
                   ignoreCell(S::Idle), ignoreCell(S::Idle), ignoreCell(S::Idle), ignoreCell(S::Idle)},
    /* AuthorizationPending */
    Row{Cell{Kind::Stay, S::AuthorizationPending, "rejectRequest"},
        Cell{Kind::Guarded, S::AwaitingRetrieval, "[authorizedUser] unlockCompartment"},
        Cell{Kind::Go, S::Rejected, "rejectRequest, logAttempt"},
        ignoreCell(S::AuthorizationPending),
        Cell{Kind::Go, S::Cancelled, "logCancellation"},
        ignoreCell(S::AuthorizationPending),
        ignoreCell(S::AuthorizationPending),
        ignoreCell(S::AuthorizationPending),
        Cell{Kind::Go, S::Interrupted, "disableDispensing, logFault"}},
    /* AwaitingRetrieval */
    Row{Cell{Kind::Stay, S::AwaitingRetrieval, "rejectRequest"},
        ignoreCell(S::AwaitingRetrieval),
        ignoreCell(S::AwaitingRetrieval),
        Cell{Kind::Go, S::AwaitingClosure, "updateInventory"},
        Cell{Kind::Go, S::Cancelled, "lockCompartment, logCancellation"},
        Cell{Kind::Go, S::TimedOut, "lockCompartment, logFailedDispense"},
        Cell{Kind::Ignore, S::AwaitingRetrieval, "ignore (drawer not opened yet / nothing removed)"},
        ignoreCell(S::AwaitingRetrieval),
        kInterrupt},
    /* AwaitingClosure */
    Row{Cell{Kind::Stay, S::AwaitingClosure, "rejectRequest"},
        ignoreCell(S::AwaitingClosure),
        ignoreCell(S::AwaitingClosure),
        Cell{Kind::Stay, S::AwaitingClosure, "updateInventory (more units taken)"},
        Cell{Kind::Reject, S::AwaitingClosure, "reject (close the drawer)"},
        ignoreCell(S::AwaitingClosure),
        Cell{Kind::Guarded, S::Completed, "[removed == requested] lockCompartment, logDispense; else raise IncompleteDispense"},
        Cell{Kind::Go, S::TimedOut, "logFault"},
        kInterrupt},
    /* Rejected */ terminalRow(S::Rejected),
    /* Cancelled */ terminalRow(S::Cancelled),
    /* TimedOut */ terminalRow(S::TimedOut),
    /* Completed */ terminalRow(S::Completed),
    /* Interrupted */ terminalRow(S::Interrupted),
}};

const Cell& cellFor(DispenseState s, DispenseEvent e) {
    return kTable[static_cast<std::size_t>(s)][static_cast<std::size_t>(e)];
}

bool parseInt(const std::string& text, int& out) {
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, out);
    return !text.empty() && r.ec == std::errc() && r.ptr == end;
}

std::string seconds(std::chrono::milliseconds ms) { return std::to_string(ms.count() / kMillisPerSecond) + " s"; }

}  // namespace

DispensingController::DispensingController(AccessControlManager& access, InventoryManager& inventory,
                                           OperationalModeManager& modes, FaultDetector& faults,
                                           HardwareInterface& hardware, EventLogger& logger, Storage& storage,
                                           const Clock& clock, const AdcConfig& config)
    : access_(access),
      inventory_(inventory),
      modes_(modes),
      faults_(faults),
      hardware_(hardware),
      logger_(logger),
      storage_(storage),
      clock_(clock),
      config_(config) {}

// ---- table driver -------------------------------------------------------------------------------
OpResult DispensingController::fire(DispenseEvent event, const std::string& reason, const Guard& guard,
                                    const Otherwise& otherwise) {
    const int row = static_cast<int>(state_);
    const int col = static_cast<int>(event);
    if (row < 0 || row >= kStateCount || col < 0 || col >= kEventCount) {
        failSafe(event);
        return OpResult::failure(ResultCode::InvalidEvent, "Internal error: dispensing disabled.");
    }
    const Cell& cell = kTable[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
    switch (cell.kind) {
        case Kind::Guarded:
            if (guard && !guard()) {
                if (otherwise) otherwise();
                return OpResult::failure(ResultCode::InvalidEvent, "");
            }
            [[fallthrough]];
        case Kind::Go:
            previous_ = state_;
            state_ = cell.target;
            enter(state_, reason);
            return OpResult::success("");
        case Kind::Stay:
            return OpResult::success("");
        case Kind::Ignore:
            if (event == DispenseEvent::CancelRequest)
                return OpResult::failure(ResultCode::InvalidEvent, "There is no request to cancel.");
            return OpResult::failure(ResultCode::InvalidEvent, "");
        case Kind::Reject:
            logger_.record(EventType::UNEXPECTED_EVENT, user_.empty() ? kSystem : user_, "REJECTED",
                           std::string("SM2 state=") + toString(state_) + " event=" + toString(event) +
                               (reason.empty() ? "" : " " + reason));
            if (event == DispenseEvent::CancelRequest)
                return OpResult::failure(ResultCode::InvalidEvent,
                                         "Medication has already been removed. Close the drawer to finish this request.");
            return OpResult::failure(ResultCode::InvalidEvent, "Unexpected event ignored.");
        case Kind::Illegal:
            failSafe(event);
            return OpResult::failure(ResultCode::InvalidEvent, "Internal error: dispensing disabled.");
    }
    return OpResult::failure(ResultCode::InvalidEvent, "");
}

// Entry actions: one place per state.
void DispensingController::enter(DispenseState state, const std::string& reason) {
    switch (state) {
        case DispenseState::Idle:
        case DispenseState::AuthorizationPending:  // beginSession already ran
            return;
        case DispenseState::AwaitingRetrieval: {   // unlockCompartment
            hardware_.o_setLock(drawer_, LockCommand::Unlocked);
            unlocked_ = true;
            tracking_ = true;
            trackedDrawer_ = drawer_;
            trackedMedication_ = medication_;
            trackedUser_ = user_;
            const auto count = hardware_.i_itemCount(drawer_);
            lastRecordedCount_ = count ? *count : inventory_.quantityOf(medication_);  // baseline
            trackedWasOpen_ = false;
            deadline_ = clock_.now() + config_.retrievalTimeout;
            persist();
            return;
        }
        case DispenseState::AwaitingClosure:
            deadline_ = clock_.now() + config_.closureTimeout;
            persist();
            return;
        case DispenseState::Rejected:
        case DispenseState::Cancelled:
        case DispenseState::TimedOut:
        case DispenseState::Completed:
        case DispenseState::Interrupted:
            finish(reason);
            return;
    }
}

// Common entry action of every terminal state: lockCompartment (INV-DSP-1), exactly one outcome
// record (INV-DSP-2), clear the saved session, notify, and the C5 fault for timeouts.
void DispensingController::finish(const std::string& reason) {
    if (unlocked_) {
        hardware_.o_setLock(drawer_, LockCommand::Locked);
        unlocked_ = false;
    }
    if (kind_ == SessionKind::Dispense) {
        logger_.recordDispense(user_, medication_, requested_, moved_, state_, reason);
    } else {
        logger_.recordReplenish(user_, medication_, moved_, state_, reason);
    }
    persist();
    const SessionInfo info = session();
    for (const OutcomeListener& listener : listeners_) listener(info, reason);
    if (state_ == DispenseState::TimedOut) {
        if (kind_ == SessionKind::Dispense) {  // C5 / FR-FAULT-004 / FR-DISP-007
            faults_.report(FaultType::IncompleteDispense, drawer_,
                           "requested=" + std::to_string(requested_) + " removed=" + std::to_string(moved_) + " " + reason);
        } else if (previous_ == DispenseState::AwaitingClosure) {  // replenish drawer left open
            faults_.report(FaultType::StorageNotClosed, drawer_, "drawer left open after replenishment");
        }
    }
}

void DispensingController::failSafe(DispenseEvent event) {
    failSafeTripped_ = true;
    logger_.record(EventType::UNEXPECTED_EVENT, kSystem, "ILLEGAL",
                   std::string("SM2 state=") + toString(state_) + " event=" + toString(event) + " fail-safe tripped");
    if (unlocked_) {
        hardware_.o_setLock(drawer_, LockCommand::Locked);
        unlocked_ = false;
    }
}

// ---- requests -----------------------------------------------------------------------------------
void DispensingController::beginSession(SessionKind kind, const MedicationId& medication, int quantity) {
    kind_ = kind;
    user_ = access_.isAuthenticated() ? access_.currentUser() : Badge("-");
    medication_ = medication;
    drawer_.clear();
    requested_ = kind == SessionKind::Dispense ? quantity : 0;
    moved_ = 0;
    unlocked_ = false;
    deadline_ = clock_.now();
    lastSeen_.clear();
}

OpResult DispensingController::rejectAttempt(SessionKind kind, const MedicationId& medication, int quantity,
                                             ResultCode code, const std::string& reason) {
    // A refused attempt while another session is active: its own single outcome record (INV-DSP-2),
    // the running session is untouched (SM2 "stay / rejectRequest").
    const Badge who = access_.isAuthenticated() ? access_.currentUser() : Badge("-");
    if (kind == SessionKind::Dispense) {
        logger_.recordDispense(who, medication, quantity, 0, DispenseState::Rejected, reason);
    } else {
        logger_.recordReplenish(who, medication, 0, DispenseState::Rejected, reason);
    }
    return OpResult::failure(code, reason);
}

bool DispensingController::drawerReady(const DrawerId& d) const {
    const auto open = hardware_.i_drawerOpen(d);
    const auto locked = hardware_.i_lockEngaged(d);
    const auto count = hardware_.i_itemCount(d);
    return open && locked && count && !*open && *locked;
}

OpResult DispensingController::evaluateRequest(SessionKind kind, const MedicationId& medication, int quantity) {
    const bool dispense = kind == SessionKind::Dispense;
    OpResult deny = OpResult::success("");
    // DSP-REQ / REP-REQ, first true row wins.
    const OpResult auth = dispense ? access_.authorize(Permission::Dispense, "request medication")
                                   : inventory_.replenishAuthorized();
    if (!auth.ok()) {  // R1
        deny = auth;
    } else if (tracking_) {  // R2
        deny = OpResult::failure(ResultCode::SessionActive, "Another drawer access is in progress. Close drawer " +
                                                                trackedDrawer_ + " first.");
    } else if (!inventory_.hasMedication(medication)) {  // R3
        deny = OpResult::failure(ResultCode::UnknownMedication,
                                 "Medication '" + medication + "' is not stocked in this cabinet.");
    } else if (dispense && quantity < 1) {  // R4
        deny = OpResult::failure(ResultCode::InvalidQuantity, "Please request a quantity of 1 or more.");
    } else if (failSafeTripped_ || !(dispense ? modes_.dispensingAllowed() : modes_.replenishmentAllowed())) {  // R5
        deny = OpResult::failure(ResultCode::ModeNotAllowed, std::string(dispense ? "Dispensing" : "Replenishment") +
                                                                 " is not available in " + toString(modes_.mode()) +
                                                                 " mode.");
    } else if (dispense && !inventory_.canDispense(medication, quantity).ok()) {  // R6
        deny = inventory_.canDispense(medication, quantity);
    } else if (!drawerReady(*inventory_.locationOf(medication))) {  // R7
        deny = OpResult::failure(ResultCode::DrawerNotReady,
                                 "Drawer " + *inventory_.locationOf(medication) + " is not ready. Call a technician.");
    }
    if (!deny.ok()) {
        fire(DispenseEvent::AuthorizationDenied, deny.message);
        return deny;
    }
    // R8
    drawer_ = *inventory_.locationOf(medication);
    const Permission needed = dispense ? Permission::Dispense : Permission::Replenish;
    fire(DispenseEvent::AuthorizationApproved, "authorized", [this, needed] { return access_.hasPermission(needed); },
         [this] { fire(DispenseEvent::AuthorizationDenied, "user no longer authorized"); });
    if (state_ != DispenseState::AwaitingRetrieval)
        return OpResult::failure(ResultCode::PermissionDenied, "The request was not authorised.");
    const std::string name = inventory_.nameOf(medication);
    if (dispense)
        return OpResult::success("Drawer " + drawer_ + " is unlocked. Take " + std::to_string(quantity) + " x " +
                                 name + ", then close the drawer (" + seconds(config_.retrievalTimeout) + ").");
    return OpResult::success("Drawer " + drawer_ + " is unlocked. Add the new stock of " + name +
                             ", then close the drawer (" + seconds(config_.retrievalTimeout) + ").");
}

OpResult DispensingController::requestDispense(const MedicationId& medication, int quantity) {
    if (cellFor(state_, DispenseEvent::DispenseRequest).kind == Kind::Stay) {  // a session is active
        const OpResult auth = access_.authorize(Permission::Dispense, "request medication");
        if (!auth.ok()) return rejectAttempt(SessionKind::Dispense, medication, quantity, auth.code, auth.message);  // R1
        return rejectAttempt(SessionKind::Dispense, medication, quantity, ResultCode::SessionActive,
                             "Another drawer access is in progress. Finish or cancel it first.");  // R2
    }
    beginSession(SessionKind::Dispense, medication, quantity);
    fire(DispenseEvent::DispenseRequest, "new request");
    return evaluateRequest(SessionKind::Dispense, medication, quantity);
}

OpResult DispensingController::requestReplenish(const MedicationId& medication) {
    if (cellFor(state_, DispenseEvent::DispenseRequest).kind == Kind::Stay) {
        const OpResult auth = inventory_.replenishAuthorized();
        if (!auth.ok()) return rejectAttempt(SessionKind::Replenish, medication, 0, auth.code, auth.message);
        return rejectAttempt(SessionKind::Replenish, medication, 0, ResultCode::SessionActive,
                             "Another drawer access is in progress. Finish or cancel it first.");
    }
    beginSession(SessionKind::Replenish, medication, 0);
    fire(DispenseEvent::DispenseRequest, "new replenish request");
    return evaluateRequest(SessionKind::Replenish, medication, 0);
}

OpResult DispensingController::cancel() {
    const DispenseState before = state_;
    const OpResult r = fire(DispenseEvent::CancelRequest, "cancelled by user");
    if (r.ok() && state_ == DispenseState::Cancelled && before != DispenseState::Cancelled)
        return OpResult::success("Request cancelled. Drawer " + drawer_ + " is locking.");
    return r;
}

// ---- sensing ------------------------------------------------------------------------------------
void DispensingController::trackCounts() {
    const DrawerId d = trackedDrawer_;
    const auto count = hardware_.i_itemCount(d);
    if (count && *count != lastRecordedCount_) {
        const int delta = *count - lastRecordedCount_;
        const bool active = sessionActive();
        const std::string why = active ? (kind_ == SessionKind::Dispense ? "dispense" : "replenish")
                                       : "change after the request ended";
        const OpResult r = delta < 0 ? inventory_.recordRemoval(trackedMedication_, -delta, trackedUser_, why)
                                     : inventory_.recordAddition(trackedMedication_, delta, trackedUser_, why);
        if (!r.ok()) {
            // lastRecordedCount_ is kept, so the change is retried on the next tick.
            faults_.report(FaultType::InventoryUpdateFailed, d, r.message);
        } else {
            lastRecordedCount_ = *count;
            if (active) {
                moved_ += kind_ == SessionKind::Dispense ? -delta : delta;
                persist();
                if (kind_ == SessionKind::Replenish || delta < 0)
                    fire(DispenseEvent::MedicationRemoved, "count " + std::to_string(*count));
            }
        }
    }
    const auto open = hardware_.i_drawerOpen(d);
    if (open && *open) trackedWasOpen_ = true;
    if (open && !*open && trackedWasOpen_) {
        trackedWasOpen_ = false;
        if (sessionActive()) {
            // DSP-CLOSE: R1 removed == requested (or a replenish) -> Completed;
            //            R2 otherwise -> IncompleteDispense fault -> SM2 faultDetected -> Interrupted.
            fire(DispenseEvent::CompartmentClosed, "drawer closed",
                 [this] { return kind_ == SessionKind::Replenish || moved_ == requested_; },
                 [this] {
                     faults_.report(FaultType::IncompleteDispense, drawer_,
                                    "requested=" + std::to_string(requested_) + " removed=" + std::to_string(moved_) +
                                        " at closure");
                 });
        }
    }
    // A finished session's drawer is released once it is closed.
    if (tracking_ && !sessionActive() && open && !*open) tracking_ = false;
}

void DispensingController::watchUntrackedDrawers() {
    for (const DrawerId& d : hardware_.drawers()) {
        if (tracking_ && d == trackedDrawer_) continue;
        const auto count = hardware_.i_itemCount(d);
        if (!count) continue;
        const auto seen = lastSeen_.find(d);
        if (seen != lastSeen_.end() && *count < seen->second)
            fire(DispenseEvent::MedicationRemoved, "unexpected removal from drawer " + d);  // logUnexpectedEvent
        lastSeen_[d] = *count;
    }
}

void DispensingController::tick() {
    if (tracking_) trackCounts();
    const auto now = clock_.now();
    if (state_ == DispenseState::AwaitingRetrieval && now >= deadline_) {
        fire(DispenseEvent::RetrievalTimeout, "not removed within " + seconds(config_.retrievalTimeout));
    } else if (state_ == DispenseState::AwaitingClosure && now >= deadline_) {
        fire(DispenseEvent::ClosureTimeout, "drawer not closed within " + seconds(config_.closureTimeout));
    }
    if (!sessionActive()) watchUntrackedDrawers();
}

void DispensingController::onFaultDetected(const Fault& fault) {
    if (!fault.critical) return;  // NFR-AVL-004: communication loss does not stop local operation
    fire(DispenseEvent::FaultDetected,
         std::string("fault ") + toString(fault.type) + (fault.drawer.empty() ? "" : " drawer=" + fault.drawer));
}

// ---- persistence (NFR-SAF-003) ------------------------------------------------------------------
void DispensingController::persist() {
    std::vector<Storage::Row> rows;
    if (sessionActive()) {
        rows.push_back({kind_ == SessionKind::Dispense ? kDispenseKind : kReplenishKind, user_, medication_, drawer_,
                        std::to_string(requested_), std::to_string(moved_), toString(state_), unlocked_ ? "1" : "0"});
    }
    storage_.saveSnapshot(kSessionFile, rows);
}

void DispensingController::recoverInterruptedSession() {
    const auto rows = storage_.loadSnapshot(kSessionFile);
    if (rows.empty()) return;
    const Storage::Row& row = rows.front();
    DispenseState saved = DispenseState::Idle;
    int requested = 0;
    int moved = 0;
    const bool valid = row.size() == kSessionFields && (row[0] == kDispenseKind || row[0] == kReplenishKind) &&
                       parseInt(row[4], requested) && parseInt(row[5], moved) && fromString(row[6], saved);
    if (!valid) {
        logger_.record(EventType::UNEXPECTED_EVENT, kSystem, "DISCARDED", "unreadable saved session");
        storage_.saveSnapshot(kSessionFile, {});
        return;
    }
    kind_ = row[0] == kDispenseKind ? SessionKind::Dispense : SessionKind::Replenish;
    user_ = row[1];
    medication_ = row[2];
    drawer_ = row[3];
    requested_ = requested;
    moved_ = moved;
    state_ = saved;
    if (!sessionActive()) {
        storage_.saveSnapshot(kSessionFile, {});
        return;
    }
    unlocked_ = true;  // command LOCKED on the way to Interrupted
    tracking_ = true;  // keep recording any change until the drawer is closed
    trackedDrawer_ = drawer_;
    trackedMedication_ = medication_;
    trackedUser_ = user_;
    lastRecordedCount_ = inventory_.quantityOf(medication_);  // records are the baseline after a restart
    trackedWasOpen_ = false;
    fire(DispenseEvent::FaultDetected, "ADC restarted during the request; a new request is required");
}

// ---- queries ------------------------------------------------------------------------------------
DispenseState DispensingController::state() const { return state_; }

bool DispensingController::sessionActive() const {
    return state_ == DispenseState::AuthorizationPending || state_ == DispenseState::AwaitingRetrieval ||
           state_ == DispenseState::AwaitingClosure;
}

bool DispensingController::busy() const { return sessionActive() || tracking_; }

std::set<DrawerId> DispensingController::drawersUnderControl() const {
    if (!tracking_) return {};
    return {trackedDrawer_};
}

SessionInfo DispensingController::session() const {
    SessionInfo s;
    s.kind = kind_;
    s.state = state_;
    s.user = user_;
    s.medication = medication_;
    s.drawer = drawer_;
    s.requested = requested_;
    s.moved = moved_;
    const bool timed = state_ == DispenseState::AwaitingRetrieval || state_ == DispenseState::AwaitingClosure;
    const auto left = deadline_ - clock_.now();
    s.timeLeft = (timed && left.count() > 0) ? left : std::chrono::milliseconds(0);
    s.drawerUnderControl = tracking_;
    s.controlledDrawer = tracking_ ? trackedDrawer_ : DrawerId();
    return s;
}

DispenseCell DispensingController::transitionFor(DispenseState state, DispenseEvent event) {
    static const char* const kKindNames[] = {"Go", "Guarded", "Stay", "Ignore", "Reject", "Illegal"};
    const Cell& c = cellFor(state, event);
    return DispenseCell{kKindNames[static_cast<int>(c.kind)], c.target, c.actions};
}

void DispensingController::addOutcomeListener(OutcomeListener listener) { listeners_.push_back(std::move(listener)); }

}  // namespace adc
