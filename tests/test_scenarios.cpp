// The 12 demo scenarios of the brief (section 7), end to end through AdcSystem's menu with
// scripted input and a ManualClock. Menu scripts use the same keys a person types.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

#include "adc/adc_system.hpp"
#include "adc/clock.hpp"
#include "adc/hardware_interface.hpp"
#include "test_helpers.hpp"

using namespace adc;

namespace {
// Menu scripts.
const std::string kNurse = "1\nN100\n1234\n";
const std::string kPharmacist = "1\nP200\n2345\n";
const std::string kTechnician = "1\nT300\n3456\n";
std::string request(const std::string& med, int qty) { return "4\n" + med + "\n" + std::to_string(qty) + "\n"; }
std::string replenish(const std::string& med) { return "5\n" + med + "\n"; }
std::string open(const std::string& d) { return "10\n1\n" + d + "\n"; }
std::string close(const std::string& d) { return "10\n2\n" + d + "\n"; }
std::string take(const std::string& d, int n) { return "10\n3\n" + d + "\n" + std::to_string(n) + "\n"; }
std::string add(const std::string& d, int n) { return "10\n4\n" + d + "\n" + std::to_string(n) + "\n"; }
std::string forceOpen(const std::string& d) { return "10\n5\n" + d + "\n"; }
std::string jamLock(const std::string& d) { return "10\n6\n" + d + "\n"; }
std::string repairLock(const std::string& d) { return "10\n7\n" + d + "\n"; }
std::string failSensor(const std::string& d) { return "10\n8\n" + d + "\n"; }
std::string repairSensor(const std::string& d) { return "10\n9\n" + d + "\n"; }
const std::string kOffline = "11\n1\n";
const std::string kOnline = "11\n2\n";
const std::string kViewLog = "7\n";
const std::string kEnterMaintenance = "8\n1\n";
const std::string kExitMaintenance = "8\n2\n";
const std::string kDiagnostics = "8\n3\n";
const std::string kClearFault = "9\n";
const std::string kRestart = "12\n";
}  // namespace

class ScenarioTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    std::stringstream in;
    std::ostringstream out;
    std::unique_ptr<AdcSystem> sys;

    void SetUp() override {
        sys = std::make_unique<AdcSystem>(cfg, clock, in, out, false);
        wait(cfg.tickPeriod);  // first exchange with the Monitoring System
    }
    // Types the script into the menu and processes it completely.
    void type(const std::string& script) {
        in.clear();
        in << script;
        while (true) {
            in.clear();
            if (in.peek() == std::char_traits<char>::eof()) break;
            if (!sys->runOnce()) break;
        }
        in.clear();
    }
    void wait(Millis duration) {
        for (Millis t{0}; t < duration; t += cfg.tickPeriod) {
            clock.advance(cfg.tickPeriod);
            sys->tick();
        }
    }
    std::string screen() {
        const std::string s = out.str();
        out.str("");
        return s;
    }
    int count(EventType t, const std::string& outcome = "") const {
        int n = 0;
        for (const auto& r : sys->events()) n += (r.type == t && (outcome.empty() || r.outcome == outcome)) ? 1 : 0;
        return n;
    }
    bool monitorGotEvent(const std::string& eventType, const std::string& extra = "") const {
        for (const auto& m : sys->monitorReceived()) {
            if (m.type == "event" && m.json.find("\"event_type\":\"" + eventType + "\"") != std::string::npos &&
                m.json.find(extra) != std::string::npos)
                return true;
        }
        return false;
    }
    int monitorCount(const std::string& type) const {
        int n = 0;
        for (const auto& m : sys->monitorReceived()) n += m.type == type ? 1 : 0;
        return n;
    }
    LockCommand commanded(int index) const { return sys->cabinet().drawers.at(static_cast<std::size_t>(index)).commanded; }
};

// 1. Authenticate -> dispense -> log (D1 Activity 1; US-AUTH-01, US-DISP-01, US-LOG-01)
TEST_F(ScenarioTest, Scenario01_AuthenticateDispenseLog) {
    type(kNurse);
    type(request("M03", 2));
    EXPECT_NE(screen().find("[OK] Drawer D3 is unlocked"), std::string::npos);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(commanded(i), i == 2 ? LockCommand::Unlocked : LockCommand::Locked);
    type(open("D3") + take("D3", 2));
    EXPECT_EQ(sys->quantityOf("M03"), 8);  // before closing
    type(close("D3"));
    EXPECT_NE(screen().find("[OK] Dispense complete: 2 x M03 removed. Drawer D3 is locking."), std::string::npos);
    EXPECT_TRUE(sys->cabinet().drawers[2].lockEngaged);
    EXPECT_EQ(count(EventType::AUTH_SUCCESS), 1);
    EXPECT_EQ(count(EventType::INVENTORY_CHANGED), 1);
    EXPECT_EQ(count(EventType::DISPENSE_OUTCOME, "COMPLETED"), 1);
    wait(Millis(1000));
    EXPECT_TRUE(monitorGotEvent("AUTH_SUCCESS"));
    EXPECT_TRUE(monitorGotEvent("INVENTORY_CHANGED"));
    EXPECT_TRUE(monitorGotEvent("DISPENSE_OUTCOME", "COMPLETED"));
}

// 2. Invalid credentials (US-AUTH-01 AC-03)
TEST_F(ScenarioTest, Scenario02_InvalidCredentials) {
    type("1\nN100\n9999\n");
    EXPECT_NE(screen().find("[ERROR] Sign-in failed: badge or PIN not recognised."), std::string::npos);
    EXPECT_FALSE(sys->signedIn());
    ASSERT_EQ(count(EventType::AUTH_FAILURE), 1);
    for (const auto& r : sys->events()) {
        if (r.type == EventType::AUTH_FAILURE) {
            EXPECT_EQ(r.user, "N100");
        }
        EXPECT_EQ(r.details.find("9999"), std::string::npos);
    }
}

// 3. Permission denied (US-SEC-01 AC-02, US-MODE-01 AC-03)
TEST_F(ScenarioTest, Scenario03_PermissionDenied) {
    type(kNurse);
    screen();
    type("5\n");  // denied before any medication is asked for
    EXPECT_NE(screen().find("[ERROR] You are not authorised to replenish medication."), std::string::npos);
    type(kViewLog);
    EXPECT_NE(screen().find("[ERROR] You are not authorised to view the event log."), std::string::npos);
    type(kEnterMaintenance);
    EXPECT_NE(screen().find("[ERROR] You are not authorised to enter maintenance mode."), std::string::npos);
    EXPECT_EQ(count(EventType::ACCESS_DENIED), 3);
    EXPECT_EQ(sys->mode(), Mode::Normal);
}

// 4. Unavailable medication (US-DISP-01 AC-02)
TEST_F(ScenarioTest, Scenario04_UnavailableMedication) {
    type(kNurse);
    type(request("M03", 999));
    EXPECT_NE(screen().find("[ERROR] Not enough Morphine 2 mg/mL injection: 10 recorded, 999 requested."),
              std::string::npos);
    EXPECT_EQ(count(EventType::DISPENSE_OUTCOME, "REJECTED"), 1);
    EXPECT_EQ(commanded(2), LockCommand::Locked);
}

// 5. Hardware fault -> Fault mode -> clear (D1 Activity 2; US-FAULT-01 AC-02, US-SAF-01, FR-MODE-005)
TEST_F(ScenarioTest, Scenario05_HardwareFaultFaultModeClear) {
    type(kNurse + request("M03", 2) + jamLock("D3") + open("D3") + take("D3", 2) + close("D3"));
    wait(cfg.lockConfirmTimeout + cfg.tickPeriod);
    const std::string s = screen();
    EXPECT_NE(s.find("!!! WARNING: DISPENSING DISABLED - SAFETY FAULT !!!"), std::string::npos);
    EXPECT_NE(s.find("A drawer did not lock (drawer D3)"), std::string::npos);
    EXPECT_EQ(sys->mode(), Mode::Fault);
    EXPECT_EQ(sys->cabinet().indicator, IndicatorLevel::Fault);
    EXPECT_TRUE(signalFor(sys->cabinet().indicator).buzzer);
    wait(Millis(400));
    EXPECT_GE(monitorCount("alarm"), 1);
    type(request("M01", 1));
    EXPECT_NE(screen().find("[ERROR] Dispensing is not available in FAULT mode."), std::string::npos);
    type(kClearFault);
    EXPECT_NE(screen().find("[ERROR] You are not authorised to clear a fault."), std::string::npos);
    type(kTechnician + kClearFault);
    EXPECT_NE(screen().find("[ERROR] The fault is still present"), std::string::npos);
    EXPECT_EQ(count(EventType::FAULT_CLEAR_DENIED), 1);
    EXPECT_EQ(sys->mode(), Mode::Fault);
    type(repairLock("D3") + kClearFault);
    EXPECT_NE(screen().find("[OK] Fault cleared. The cabinet is back in NORMAL mode."), std::string::npos);
    EXPECT_EQ(sys->mode(), Mode::Normal);
}

// 6. Incomplete dispense (FR-DISP-007, FR-FAULT-004, C5)
TEST_F(ScenarioTest, Scenario06_IncompleteDispenseTimedOutFaultMode) {
    type(kNurse + request("M03", 1) + open("D3"));
    screen();
    wait(cfg.retrievalTimeout);
    const std::string s = screen();
    EXPECT_NE(s.find("timed out"), std::string::npos);
    EXPECT_NE(s.find("!!! WARNING: DISPENSING DISABLED"), std::string::npos);
    EXPECT_EQ(sys->dispenseState(), DispenseState::TimedOut);
    EXPECT_EQ(sys->mode(), Mode::Fault);
    EXPECT_EQ(count(EventType::DISPENSE_OUTCOME, "TIMED_OUT"), 1);
    EXPECT_EQ(count(EventType::FAULT_DETECTED, "IncompleteDispense"), 1);
}

// 7. Interrupted dispense, no second dose (NFR-SAF-003, US-SAF-01 AC-03)
TEST_F(ScenarioTest, Scenario07_InterruptedNoSecondDose) {
    type(kNurse + request("M03", 2) + open("D3") + failSensor("D3"));
    EXPECT_NE(screen().find("interrupted"), std::string::npos);
    EXPECT_EQ(sys->dispenseState(), DispenseState::Interrupted);
    EXPECT_EQ(commanded(2), LockCommand::Locked);
    type(close("D3"));
    EXPECT_TRUE(sys->cabinet().drawers[2].lockEngaged);  // the drawer locks on close
    type(request("M03", 2));
    EXPECT_NE(screen().find("[ERROR]"), std::string::npos);  // a new request is refused while in Fault
    type(repairSensor("D3") + kTechnician + kClearFault);
    ASSERT_EQ(sys->mode(), Mode::Normal);
    type(kNurse + request("M03", 2));
    EXPECT_NE(screen().find("[OK] Drawer D3 is unlocked"), std::string::npos);  // only a NEW authorised request
    EXPECT_EQ(count(EventType::DISPENSE_OUTCOME, "INTERRUPTED"), 1);
}

// 8. Communication loss (D1 Activity 3; US-COM-01 AC-02, US-AVL-01, NFR-PRF-003)
TEST_F(ScenarioTest, Scenario08_CommunicationLoss) {
    ASSERT_EQ(sys->link(), LinkState::Connected);
    type(kNurse + kOffline);
    const auto start = clock.now();
    while (sys->link() == LinkState::Connected && clock.now() - start < Millis(10000)) wait(cfg.tickPeriod);
    EXPECT_EQ(sys->link(), LinkState::Disconnected);
    EXPECT_LT(clock.now() - start, Millis(5000));
    EXPECT_NE(screen().find("WARNING: Monitoring link down: cabinet working locally."), std::string::npos);
    EXPECT_EQ(sys->mode(), Mode::Normal);
    EXPECT_EQ(sys->cabinet().indicator, IndicatorLevel::Warning);
    const int unsentBefore = sys->unsentEvents();
    type(request("M03", 2) + open("D3") + take("D3", 2) + close("D3"));
    EXPECT_EQ(sys->dispenseState(), DispenseState::Completed);
    EXPECT_GT(sys->unsentEvents(), unsentBefore);
}

// 9. Restoration (FR-COM-005, US-COM-01 AC-03)
TEST_F(ScenarioTest, Scenario09_Restoration) {
    type(kNurse + kOffline);
    wait(Millis(4000));
    type(request("M03", 1) + open("D3") + take("D3", 1) + close("D3"));
    ASSERT_EQ(sys->link(), LinkState::Disconnected);
    screen();
    type(kOnline);
    wait(Millis(3000));
    const std::string s = screen();
    EXPECT_NE(s.find("DISCONNECTED -> RESYNCHRONIZING"), std::string::npos);
    EXPECT_NE(s.find("RESYNCHRONIZING -> CONNECTED"), std::string::npos);
    EXPECT_EQ(sys->link(), LinkState::Connected);
    EXPECT_EQ(sys->unsentEvents(), 0);
    EXPECT_EQ(sys->unsentMessages(), 0);
    std::vector<long long> ids;
    for (const auto& m : sys->monitorReceived()) {
        if (m.type == "event") ids.push_back(std::stoll(m.id.substr(4)));
    }
    ASSERT_FALSE(ids.empty());
    for (std::size_t i = 0; i < ids.size(); ++i) EXPECT_EQ(ids[i], static_cast<long long>(i + 1));  // in order, none missing
    EXPECT_EQ(static_cast<long long>(ids.size()), static_cast<long long>(sys->events().size()));
    EXPECT_EQ(sys->monitorDuplicates(), 0);
}

// 10. Replenish (D1 Replenish activity; FR-INV-003)
TEST_F(ScenarioTest, Scenario10_Replenish) {
    type(kPharmacist + replenish("M04") + open("D4") + add("D4", 8) + close("D4"));
    EXPECT_NE(screen().find("[OK] Replenishment complete: 8 x M04 added."), std::string::npos);
    EXPECT_EQ(sys->quantityOf("M04"), 20);
    EXPECT_EQ(count(EventType::REPLENISH_OUTCOME, "COMPLETED"), 1);
    wait(Millis(1000));
    bool sent = false;
    for (const auto& m : sys->monitorReceived()) {
        if (m.type == "inventory" && m.json.find("\"medication\":\"M04\"") != std::string::npos &&
            m.json.find("\"quantity\":20") != std::string::npos)
            sent = true;
    }
    EXPECT_TRUE(sent);
}

// 11. Maintenance (FR-MODE-003/004)
TEST_F(ScenarioTest, Scenario11_Maintenance) {
    type(kTechnician + kEnterMaintenance);
    EXPECT_EQ(sys->mode(), Mode::Maintenance);
    type(kDiagnostics);
    EXPECT_NE(screen().find("[OK] Diagnostics: all drawer checks passed."), std::string::npos);
    type(kNurse + request("M03", 1));
    EXPECT_NE(screen().find("[ERROR] Dispensing is not available in MAINTENANCE mode."), std::string::npos);
    type(kTechnician + kExitMaintenance);
    EXPECT_EQ(sys->mode(), Mode::Normal);
    EXPECT_EQ(count(EventType::MAINTENANCE_ACTIVITY), 3);
    wait(Millis(1000));
    EXPECT_EQ(monitorCount("maintenance"), 3);
}

// 12. Restart (NFR-REL-001, NFR-REL-004, US-REL-01)
TEST_F(ScenarioTest, Scenario12_Restart) {
    type(kNurse + request("M03", 2) + open("D3") + take("D3", 2) + close("D3"));
    const auto before = sys->events();
    type(kRestart);
    EXPECT_FALSE(sys->signedIn());
    EXPECT_EQ(sys->quantityOf("M03"), 8);
    const auto after = sys->events();
    ASSERT_GT(after.size(), before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(after[i].id, before[i].id);
        EXPECT_EQ(after[i].details, before[i].details);
    }
    EXPECT_EQ(sys->mode(), Mode::Normal);
    type(forceOpen("D2"));
    EXPECT_EQ(sys->mode(), Mode::Fault);  // UnauthorizedAccess
    type(kRestart);
    EXPECT_NE(screen().find("Start-up check"), std::string::npos);
    EXPECT_EQ(sys->mode(), Mode::Fault);
    EXPECT_GE(count(EventType::FAULT_DETECTED, "StartupCheckFailed"), 1);
}

// Restart in the middle of a dispense: closed as Interrupted, never resumed (NFR-SAF-003).
TEST_F(ScenarioTest, RestartMidDispense_InterruptedNotResumed) {
    type(kNurse + request("M03", 2) + open("D3") + take("D3", 1));
    type(kRestart);
    EXPECT_EQ(sys->dispenseState(), DispenseState::Interrupted);
    EXPECT_EQ(count(EventType::DISPENSE_OUTCOME, "INTERRUPTED"), 1);
    EXPECT_EQ(sys->quantityOf("M03"), 9);
    EXPECT_EQ(commanded(2), LockCommand::Locked);
}

// R6 / NFR-SEC-001: no PIN appears on screen, in the log, in any message or in any file.
TEST_F(ScenarioTest, NoPinAnywhere) {
    type(kNurse + "2\n" + kPharmacist + "2\n" + kTechnician + "2\n" + "1\nN101\n4321\n" + "1\nN100\n9876\n");
    wait(Millis(1000));
    const std::string shown = screen();
    std::string files;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        std::ifstream f(entry.path(), std::ios::binary);
        files.append(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    std::string events;
    for (const auto& r : sys->events()) events += r.user + r.medication + r.outcome + r.details + "\n";
    std::string sent;
    for (const auto& m : sys->monitorReceived()) sent += m.json + "\n";
    for (const std::string pin : {"1234", "2345", "3456", "4321", "9876"}) {
        EXPECT_EQ(shown.find(pin), std::string::npos) << pin;
        EXPECT_EQ(files.find(pin), std::string::npos) << pin;
        EXPECT_EQ(events.find(pin), std::string::npos) << pin;
        EXPECT_EQ(sent.find(pin), std::string::npos) << pin;
    }
}

TEST_F(ScenarioTest, ExitPowersOffCleanly) {
    type("0\n");
    EXPECT_NE(screen().find("Cabinet powered off."), std::string::npos);
    EXPECT_EQ(sys->mode(), Mode::Off);
    EXPECT_EQ(sys->link(), LinkState::Off);
}
