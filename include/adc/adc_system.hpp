#pragma once
// The ADC application: builds every module, wires the listeners and runs the control loop and
// the menu (not a D1 component; the composition root added for D2).
//
// ---- Module Interface Specification (MG 1) --------------------------------------
// Responsibility: composition root and run loop.
// Secret:         none of its own (wiring and the order of the control loop only).
// Uses:           ConsoleUI, AccessControlManager, DispensingController, OperationalModeManager,
//                 CommunicationManager, InventoryManager, FaultDetector, EventLogger,
//                 SimulatedCabinet, FakeMonitor, Storage, Clock, AdcConfig.
// State invariant(s):
//   INV-SYS-1: every module is touched only while mutex_ is held (one lock around all modules).
//   INV-SYS-2: the control loop order is dispensing.tick -> faults.check -> access.tick(busy)
//              -> comm.tick -> modes.refreshIndicator.
// Programming errors (exceptions) are caught here and shown as an internal error.

#include <condition_variable>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/console_ui.hpp"
#include "adc/fake_monitor.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/types.hpp"

namespace adc {

class AdcSystem {
public:
    // AdcSystem(config, clock, in, out, runTicker)
    // Reqs:        NFR-REL-001, NFR-REL-004, NFR-SAF-003 (start-up sequence)
    // Pre:         clock, in and out outlive this object.
    // Post:        every module is built from config.dataDir and start-up has run: SYSTEM_STARTUP
    //              logged, an interrupted session recovered, every lock commanded LOCKED, the
    //              hardware self-test run and SM1 left Startup. If runTicker, a thread calls
    //              tick() every tickPeriod.
    AdcSystem(AdcConfig config, Clock& clock, std::istream& in, std::ostream& out, bool runTicker);

    // ~AdcSystem(): stops the ticker; modules are destroyed (state is already persisted).
    ~AdcSystem();

    AdcSystem(const AdcSystem&) = delete;
    AdcSystem& operator=(const AdcSystem&) = delete;

    // run() -> exit code
    // Post:        menu interactions were processed until Exit (0) or end of input; the cabinet
    //              was powered off (SM1 / SM3 powerOff). Returns 0.
    int run();

    // runOnce() -> bool
    // Post:        draws the status bar and menu, handles one choice; false after Exit or EOF.
    bool runOnce();

    // tick()
    // Post:        one control-loop step (INV-SYS-2) under the lock; notices printed.
    void tick();

    // ---- read-only views (copies, taken under the lock) for tests and the demo --------------
    Mode mode() const;
    LinkState link() const;
    DispenseState dispenseState() const;
    int quantityOf(const MedicationId& medication) const;
    std::vector<EventRecord> events() const;
    std::vector<ReceivedMessage> monitorReceived() const;
    int monitorDuplicates() const;
    CabinetSnapshot cabinet() const;
    bool signedIn() const;
    int unsentEvents() const;
    int unsentMessages() const;

private:
    struct Parts;

    void build();
    void startup();
    void shutdown();
    void startTicker();
    void stopTicker();
    void tickLocked();
    StatusBarInfo statusLocked() const;
    void handle(int choice);
    void doSignIn();
    void doRequest();
    void doReplenish();
    void doMaintenance();
    void doHarnessCabinet();
    void doHarnessNetwork();
    void doRestart();

    AdcConfig config_;
    Clock& clock_;
    ConsoleUI ui_;
    FakeMonitor monitor_;  // the external system outlives an ADC restart
    std::unique_ptr<Parts> parts_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread ticker_;
    bool tickerEnabled_;
    bool stopTicker_ = false;
    bool poweredOff_ = false;  // set by shutdown(); the control loop then does nothing
};

}  // namespace adc
