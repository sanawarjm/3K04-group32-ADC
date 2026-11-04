#include "adc/adc_system.hpp"

#include <cctype>
#include <exception>
#include <utility>

#include "adc/access_control_manager.hpp"
#include "adc/communication_manager.hpp"
#include "adc/dispensing_controller.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/storage.hpp"

namespace adc {

namespace {

constexpr int kMaxMenuOption = 12;
constexpr int kMillisPerSecond = 1000;  // unit conversion for the screen
const char* const kSystem = "ADC";

enum MenuOption {
    kExit = 0,
    kSignIn = 1,
    kSignOut = 2,
    kViewInventory = 3,
    kRequest = 4,
    kReplenish = 5,
    kCancel = 6,
    kViewLog = 7,
    kMaintenance = 8,
    kClearFault = 9,
    kHarnessCabinet = 10,
    kHarnessNetwork = 11,
    kRestart = 12
};

enum CabinetAction {
    kOpen = 1,
    kClose,
    kTake,
    kAdd,
    kForceOpen,
    kJamLock,
    kRepairLock,
    kFailSensor,
    kRepairSensor,
    kShowDrawers,
    kCabinetActionCount = kShowDrawers
};

enum NetworkAction { kOffline = 1, kOnline, kShowReceived, kSendConfig, kNetworkActionCount = kSendConfig };

enum MaintenanceAction { kEnter = 1, kLeave, kDiagnostics, kMaintenanceActionCount = kDiagnostics };

int secondsOf(std::chrono::milliseconds ms) {
    return static_cast<int>((ms.count() + kMillisPerSecond - 1) / kMillisPerSecond);
}

}  // namespace

// Every module, in "uses" order (lower modules first, so they are destroyed last).
struct AdcSystem::Parts {
    Storage storage;
    EventLogger logger;
    SimulatedCabinet cabinet;
    AccessControlManager access;
    InventoryManager inventory;
    FaultDetector faults;
    OperationalModeManager modes;
    DispensingController dispensing;
    CommunicationManager comm;

    Parts(const AdcConfig& c, Clock& clock, FakeMonitor& monitor)
        : storage(c.dataDir),
          logger(storage, clock),
          cabinet(storage, c),
          access(logger, clock, c),
          inventory(storage, logger, access, c),
          faults(cabinet, logger, clock, c),
          modes(access, faults, logger, cabinet),
          dispensing(access, inventory, modes, faults, cabinet, logger, storage, clock, c),
          comm(monitor, logger, storage, clock, c) {}
};

AdcSystem::AdcSystem(AdcConfig config, Clock& clock, std::istream& in, std::ostream& out, bool runTicker)
    : config_(std::move(config)), clock_(clock), ui_(in, out), tickerEnabled_(runTicker) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        build();
        startup();
    }
    startTicker();
}

AdcSystem::~AdcSystem() { stopTicker(); }

// ---- wiring -------------------------------------------------------------------------------------
void AdcSystem::build() {
    parts_ = std::make_unique<Parts>(config_, clock_, monitor_);
    Parts& p = *parts_;
    // Upward notifications (a listener is not a "use").
    p.faults.addListener([this](const Fault& f) {
        Parts& q = *parts_;
        q.modes.onFaultDetected(f);
        q.dispensing.onFaultDetected(f);
        q.comm.publishAlarm(f);
        if (f.critical) ui_.showCriticalFault(f.message);
        else ui_.showWarning(f.message);
    });
    p.inventory.addChangeListener([this](const Medication& m) { parts_->comm.publishInventory(m); });
    p.modes.addModeListener([this](Mode from, Mode to, const std::string& reason) {
        parts_->comm.publishStatus(to, parts_->faults.hasCriticalFault(), reason);
        ui_.showInfo(std::string("Mode changed: ") + toString(from) + " -> " + toString(to) + " (" + reason + ")");
    });
    p.modes.addMaintenanceListener(
        [this](const std::string& activity, const Badge& by) { parts_->comm.publishMaintenance(activity, by); });
    p.comm.addLinkListener([this](LinkState from, LinkState to) {
        ui_.showInfo(std::string("Monitoring link: ") + toString(from) + " -> " + toString(to));
        if (to == LinkState::Disconnected) parts_->faults.reportCommunicationLoss();
        if (to == LinkState::Connected) {
            parts_->faults.clearCommunicationLoss();
            parts_->modes.refreshIndicator();
        }
    });
    p.dispensing.addOutcomeListener([this](const SessionInfo& s, const std::string& reason) {
        const std::string what = s.kind == SessionKind::Dispense ? "Dispense" : "Replenishment";
        switch (s.state) {
            case DispenseState::Completed:
                ui_.showSuccess(what + " complete: " + std::to_string(s.moved) + " x " + s.medication + " " +
                                (s.kind == SessionKind::Dispense ? "removed" : "added") + ". Drawer " + s.drawer +
                                " is locking.");
                break;
            case DispenseState::Cancelled:
                ui_.showInfo(what + " cancelled. Drawer " + s.drawer + " is locking.");
                break;
            case DispenseState::TimedOut:
                ui_.showError(what + " timed out (" + reason + "). A new request is required.");
                break;
            case DispenseState::Interrupted:
                ui_.showError(what + " interrupted (" + reason + "). The drawer locks when closed. "
                              "A new authorised request is required.");
                break;
            default:
                break;  // Rejected: the refusal is shown by the menu
        }
    });
}

void AdcSystem::startup() {
    Parts& p = *parts_;
    p.logger.record(EventType::SYSTEM_STARTUP, kSystem, "STARTED", "cabinet=" + config_.cabinetId);
    p.dispensing.recoverInterruptedSession();                                       // NFR-SAF-003
    for (const DrawerId& d : p.cabinet.drawers()) p.cabinet.o_setLock(d, LockCommand::Locked);
    const std::vector<Fault> failed = p.faults.selfTest(p.inventory.expectedCounts());  // NFR-REL-004
    for (const Fault& f : failed) {
        p.comm.publishAlarm(f);
        ui_.showCriticalFault(f.message + " Start-up check: " + f.detail + ".");
    }
    p.modes.completeStartup(failed);  // SM1 Startup -> Normal / Fault
    for (const Medication& m : p.inventory.medications()) p.comm.publishInventory(m);
    if (p.modes.mode() == Mode::Normal)
        ui_.showSuccess("Start-up check passed. Cabinet " + config_.cabinetId + " is ready.");
}

void AdcSystem::shutdown() {
    if (!parts_ || poweredOff_) return;
    poweredOff_ = true;
    parts_->modes.powerOff();
    parts_->comm.powerOff();
}

// ---- control loop -------------------------------------------------------------------------------
void AdcSystem::startTicker() {
    if (!tickerEnabled_ || ticker_.joinable()) return;
    stopTicker_ = false;
    ticker_ = std::thread([this] {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopTicker_) {
            wake_.wait_for(lock, config_.tickPeriod, [this] { return stopTicker_; });
            if (stopTicker_) break;
            tickLocked();
        }
    });
}

void AdcSystem::stopTicker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopTicker_ = true;
    }
    wake_.notify_all();
    if (ticker_.joinable()) ticker_.join();
}

void AdcSystem::tick() {
    std::lock_guard<std::mutex> lock(mutex_);
    tickLocked();
}

void AdcSystem::tickLocked() {
    if (!parts_ || poweredOff_) return;
    try {
        Parts& p = *parts_;
        const bool wasSignedIn = p.access.isAuthenticated();
        // INV-SYS-2
        p.dispensing.tick();
        p.faults.check(p.dispensing.drawersUnderControl(), p.inventory.expectedCounts());
        p.access.tick(p.dispensing.busy());
        p.comm.tick();
        p.modes.refreshIndicator();
        if (wasSignedIn && !p.access.isAuthenticated())
            ui_.showInfo("Signed out after " + std::to_string(secondsOf(config_.sessionTimeout)) +
                         " s of inactivity.");
    } catch (const std::exception& e) {
        ui_.showError(std::string("Internal error: ") + e.what());
    }
}

// ---- menu ---------------------------------------------------------------------------------------
StatusBarInfo AdcSystem::statusLocked() const {
    const Parts& p = *parts_;
    StatusBarInfo i;
    i.cabinetId = config_.cabinetId;
    i.mode = p.modes.mode();
    i.link = p.comm.state();
    i.unsentEvents = p.comm.unsentEventCount();
    i.unsentMessages = p.comm.unsentMessageCount();
    i.signedIn = p.access.isAuthenticated();
    i.userName = p.access.currentUserName();
    if (const auto role = p.access.currentRole()) i.role = toString(*role);
    i.secondsLeft = secondsOf(p.access.sessionRemaining());
    const CabinetSnapshot snap = p.cabinet.snapshot();
    i.indicator = snap.indicator;
    i.indicatorText = snap.indicatorText;
    const SessionInfo s = p.dispensing.session();
    if (p.dispensing.sessionActive()) {
        const std::string what = s.kind == SessionKind::Dispense
                                     ? std::to_string(s.requested) + " x " + s.medication
                                     : "replenish " + s.medication;
        i.sessionLine = what + " from " + s.drawer + " - " + toString(s.state) + " - " +
                        (s.kind == SessionKind::Dispense ? "removed " : "added ") + std::to_string(s.moved) + " - " +
                        std::to_string(secondsOf(s.timeLeft)) + " s left";
    } else if (s.drawerUnderControl) {
        i.sessionLine = "drawer " + s.controlledDrawer + " not yet seen closed: close it so it can lock";
    }
    return i;
}

bool AdcSystem::runOnce() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ui_.showStatusBar(statusLocked());
    }
    ui_.showMenu();
    const MenuInput input = ui_.promptMenuChoice(kMaxMenuOption);
    if (input.endOfInput) {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown();
        return false;
    }
    if (input.refresh) return true;
    if (input.choice == kExit) {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown();
        ui_.showInfo("Cabinet powered off. Goodbye.");
        return false;
    }
    try {
        handle(input.choice);
    } catch (const std::exception& e) {
        ui_.showError(std::string("Internal error: ") + e.what());
    }
    return true;
}

int AdcSystem::run() {
    while (runOnce()) {
    }
    return 0;
}

void AdcSystem::handle(int choice) {
    switch (choice) {
        case kSignIn: doSignIn(); return;
        case kRequest: doRequest(); return;
        case kReplenish: doReplenish(); return;
        case kMaintenance: doMaintenance(); return;
        case kHarnessCabinet: doHarnessCabinet(); return;
        case kHarnessNetwork: doHarnessNetwork(); return;
        case kRestart: doRestart(); return;
        default: break;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Parts& p = *parts_;
    switch (choice) {
        case kSignOut:
            if (!p.access.isAuthenticated()) {
                ui_.showError("Nobody is signed in.");
            } else if (p.dispensing.busy()) {
                ui_.showError("Finish or cancel the current request before signing out.");
            } else {
                p.access.logout("user signed out");
                ui_.showSuccess("Signed out.");
            }
            break;
        case kViewInventory: {
            const OpResult r = p.access.authorize(Permission::ViewInventory, "view the inventory");
            if (r.ok()) ui_.showInventory(p.inventory.medications());
            else ui_.showResult(r);
            break;
        }
        case kCancel:
            ui_.showResult(p.dispensing.cancel());
            break;
        case kViewLog: {
            const OpResult r = p.access.authorize(Permission::ViewLog, "view the event log");
            if (r.ok()) ui_.showEvents(p.logger.recent(config_.recentEventsShown));
            else ui_.showResult(r);
            break;
        }
        case kClearFault:
            ui_.showResult(p.modes.requestClearFault(p.inventory.expectedCounts()));
            break;
        default:
            break;
    }
    tickLocked();
}

void AdcSystem::doSignIn() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (parts_->dispensing.busy()) {
            ui_.showError("Finish or cancel the current request before signing in again.");
            return;
        }
    }
    const auto cred = ui_.promptCredentials();
    if (!cred) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (parts_->dispensing.busy()) {
        ui_.showError("Finish or cancel the current request before signing in again.");
        return;
    }
    ui_.showResult(parts_->access.authenticate(cred->badge, cred->pin));
    tickLocked();
}

void AdcSystem::doRequest() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!parts_->access.hasPermission(Permission::Dispense)) {  // DSP-REQ R1 without asking for details
            ui_.showResult(parts_->dispensing.requestDispense("", 0));
            tickLocked();
            return;
        }
        ui_.showInventory(parts_->inventory.medications());
    }
    const auto med = ui_.promptMedication();
    if (!med) return;
    const auto qty = ui_.promptQuantity();
    if (!qty) return;
    std::lock_guard<std::mutex> lock(mutex_);
    parts_->access.recordActivity();
    ui_.showResult(parts_->dispensing.requestDispense(*med, *qty));
    tickLocked();
}

void AdcSystem::doReplenish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!parts_->access.hasPermission(Permission::Replenish)) {  // REP-REQ R1
            ui_.showResult(parts_->dispensing.requestReplenish(""));
            tickLocked();
            return;
        }
        ui_.showInventory(parts_->inventory.medications());
    }
    const auto med = ui_.promptMedication();
    if (!med) return;
    std::lock_guard<std::mutex> lock(mutex_);
    parts_->access.recordActivity();
    ui_.showResult(parts_->dispensing.requestReplenish(*med));
    tickLocked();
}

void AdcSystem::doMaintenance() {
    ui_.showInfo("  1 Enter maintenance mode   2 Leave maintenance mode   3 Run diagnostics");
    const auto action = ui_.promptNumber("Maintenance action: ", kEnter, kMaintenanceActionCount);
    if (!action) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Parts& p = *parts_;
    if (*action == kEnter) ui_.showResult(p.modes.requestEnterMaintenance());
    if (*action == kLeave) ui_.showResult(p.modes.requestExitMaintenance());
    if (*action == kDiagnostics) ui_.showResult(p.modes.runDiagnostics(p.inventory.expectedCounts()));
    tickLocked();
}

void AdcSystem::doHarnessCabinet() {
    ui_.showInfo("  [TEST HARNESS - changes the physical cabinet; the ADC only sees it through its sensors]\n"
                 "  1 open drawer   2 close drawer   3 take N units   4 add N units   5 force drawer open\n"
                 "  6 jam lock      7 repair lock    8 fail sensors   9 repair sensors   10 show drawers");
    const auto action = ui_.promptNumber("Harness action: ", kOpen, kCabinetActionCount);
    if (!action) return;
    if (*action == kShowDrawers) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> lines;
        for (const DrawerSnapshot& d : parts_->cabinet.snapshot().drawers) {
            lines.push_back(d.id + ": " + (d.open ? "OPEN" : "closed") + ", lock " +
                            (d.lockEngaged ? "engaged" : "NOT engaged") + " (commanded " + toString(d.commanded) +
                            "), count " + std::to_string(d.count) + (d.lockJammed ? ", LOCK JAMMED" : "") +
                            (d.sensorFailed ? ", SENSORS FAILED" : ""));
        }
        ui_.showList("Simulated cabinet:", lines);
        return;
    }
    const auto drawerText = ui_.promptText("Drawer (D1-D4): ");
    if (!drawerText) return;
    std::string drawer = *drawerText;
    for (char& c : drawer) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    int n = 0;
    if (*action == kTake || *action == kAdd) {
        const auto q = ui_.promptQuantity();
        if (!q) return;
        n = *q;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    SimulatedCabinet& cab = parts_->cabinet;
    OpResult r;
    switch (*action) {
        case kOpen: r = cab.simOpen(drawer); break;
        case kClose: r = cab.simClose(drawer); break;
        case kTake: r = cab.simRemove(drawer, n); break;
        case kAdd: r = cab.simAdd(drawer, n); break;
        case kForceOpen: r = cab.simForceOpen(drawer); break;
        case kJamLock: r = cab.simJamLock(drawer, true); break;
        case kRepairLock: r = cab.simJamLock(drawer, false); break;
        case kFailSensor: r = cab.simFailSensor(drawer, true); break;
        case kRepairSensor: r = cab.simFailSensor(drawer, false); break;
        default: break;
    }
    if (r.ok()) ui_.showInfo("[harness] " + r.message);
    else ui_.showInfo("[harness] not possible: " + r.message);
    tickLocked();
}

void AdcSystem::doHarnessNetwork() {
    ui_.showInfo("  [TEST HARNESS - the stand-in Monitoring System]\n"
                 "  1 Monitoring System offline   2 Monitoring System online   3 show what it received\n"
                 "  4 send a configuration update");
    const auto action = ui_.promptNumber("Network action: ", kOffline, kNetworkActionCount);
    if (!action) return;
    std::string key;
    std::string value;
    if (*action == kSendConfig) {
        const auto k = ui_.promptText("Key (for example heartbeatPeriodMs): ");
        if (!k) return;
        const auto v = ui_.promptText("Value: ");
        if (!v) return;
        key = *k;
        value = *v;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    switch (*action) {
        case kOffline:
            monitor_.setOnline(false);
            ui_.showInfo("[harness] Monitoring System is now OFFLINE. The ADC must detect this by itself.");
            break;
        case kOnline:
            monitor_.setOnline(true);
            ui_.showInfo("[harness] Monitoring System is now ONLINE.");
            break;
        case kShowReceived: {
            std::vector<std::string> lines;
            for (const ReceivedMessage& m : monitor_.received()) lines.push_back(m.id + " " + m.type + " " + m.json);
            lines.push_back("duplicates dropped: " + std::to_string(monitor_.duplicatesDropped()));
            ui_.showList("Monitoring System received (" + std::to_string(monitor_.received().size()) +
                             " messages, in order):",
                         lines);
            break;
        }
        case kSendConfig:
            monitor_.queueConfig(key, value);
            ui_.showInfo("[harness] Configuration update queued; it arrives with the next exchange.");
            break;
        default:
            break;
    }
    tickLocked();
}

void AdcSystem::doRestart() {
    stopTicker();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ui_.showInfo("Restarting the ADC: every module is rebuilt from storage.");
        shutdown();
        parts_.reset();
        poweredOff_ = false;
        build();
        startup();
    }
    startTicker();
}

// ---- read-only views ----------------------------------------------------------------------------
Mode AdcSystem::mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->modes.mode();
}

LinkState AdcSystem::link() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->comm.state();
}

DispenseState AdcSystem::dispenseState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->dispensing.state();
}

int AdcSystem::quantityOf(const MedicationId& medication) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->inventory.quantityOf(medication);
}

std::vector<EventRecord> AdcSystem::events() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->logger.all();
}

std::vector<ReceivedMessage> AdcSystem::monitorReceived() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return monitor_.received();
}

int AdcSystem::monitorDuplicates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return monitor_.duplicatesDropped();
}

CabinetSnapshot AdcSystem::cabinet() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->cabinet.snapshot();
}

bool AdcSystem::signedIn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->access.isAuthenticated();
}

int AdcSystem::unsentEvents() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->comm.unsentEventCount();
}

int AdcSystem::unsentMessages() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parts_->comm.unsentMessageCount();
}

}  // namespace adc
