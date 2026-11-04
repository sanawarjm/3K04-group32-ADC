#include <gtest/gtest.h>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class ModeTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    Storage storage{dir.path()};
    EventLogger log{storage, clock};
    SimulatedCabinet cab{storage, cfg};
    AccessControlManager access{log, clock, cfg};
    FaultDetector faults{cab, log, clock, cfg};
    OperationalModeManager modes{access, faults, log, cab};
    std::map<DrawerId, int> expected{{"D1", 20}, {"D2", 15}, {"D3", 10}, {"D4", 12}};
    std::vector<std::pair<Mode, Mode>> changes;

    void SetUp() override {
        faults.addListener([this](const Fault& f) { modes.onFaultDetected(f); });
        modes.addModeListener([this](Mode from, Mode to, const std::string&) { changes.push_back({from, to}); });
    }
    void toNormal() { modes.completeStartup({}); }
    void toMaintenance() {
        toNormal();
        ASSERT_TRUE(access.authenticate("T300", "3456").ok());
        ASSERT_TRUE(modes.requestEnterMaintenance().ok());
    }
    void toFault() {
        toNormal();
        cab.o_setLock("D3", LockCommand::Unlocked);
        cab.simJamLock("D3", true);
        faults.report(FaultType::StorageNotClosed, "D3", "lock jammed");
    }
    int count(EventType t) const {
        int n = 0;
        for (const auto& r : log.all()) n += (r.type == t) ? 1 : 0;
        return n;
    }
    void expectInvMode1() const {
        if (modes.mode() == Mode::Normal) {
            EXPECT_FALSE(faults.hasCriticalFault());
        }
        EXPECT_EQ(modes.dispensingAllowed(), modes.mode() == Mode::Normal && !faults.hasCriticalFault());
    }
};

// ---- Startup row --------------------------------------------------------------------------------
TEST_F(ModeTest, SM1_Startup_StartupChecksPassed_Normal) {
    EXPECT_EQ(modes.mode(), Mode::Startup);
    modes.completeStartup({});
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Ok);
    ASSERT_EQ(changes.size(), 1u);
    expectInvMode1();
}

TEST_F(ModeTest, SM1_Startup_StartupCheckFailed_Fault) {
    cab.simForceOpen("D1");
    const auto failed = faults.selfTest(expected);
    modes.completeStartup(failed);
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Fault);
    EXPECT_FALSE(modes.dispensingAllowed());
}

TEST_F(ModeTest, SM1_Startup_EnterMaintenance_RejectedAndLogged) {
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    const OpResult r = modes.requestEnterMaintenance();
    EXPECT_EQ(r.code, ResultCode::InvalidEvent);
    EXPECT_EQ(modes.mode(), Mode::Startup);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(ModeTest, SM1_Startup_ExitMaintenance_RejectedAndLogged) {
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_FALSE(modes.requestExitMaintenance().ok());
    EXPECT_EQ(modes.mode(), Mode::Startup);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(ModeTest, SM1_Startup_FaultDetected_Fault) {
    faults.report(FaultType::SensorFailure, "D2", "x");
    EXPECT_EQ(modes.mode(), Mode::Fault);
}

TEST_F(ModeTest, SM1_Startup_FaultCleared_Ignore) {
    const auto before = log.lastId();
    EXPECT_FALSE(modes.requestClearFault(expected).ok());
    EXPECT_EQ(modes.mode(), Mode::Startup);
    EXPECT_EQ(log.lastId(), before);  // Ignore: no log
}

TEST_F(ModeTest, SM1_Startup_PowerOff_Off) {
    modes.powerOff();
    EXPECT_EQ(modes.mode(), Mode::Off);
    EXPECT_EQ(log.all().back().type, EventType::SYSTEM_SHUTDOWN);
}

// ---- Normal row ---------------------------------------------------------------------------------
TEST_F(ModeTest, SM1_Normal_StartupChecksPassed_Ignore) {
    toNormal();
    const auto before = log.lastId();
    modes.completeStartup({});
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(log.lastId(), before);
}

TEST_F(ModeTest, SM1_Normal_StartupCheckFailed_Ignore) {
    toNormal();
    Fault f;
    modes.completeStartup({f});
    EXPECT_EQ(modes.mode(), Mode::Normal);
}

TEST_F(ModeTest, SM1_Normal_EnterMaintenance_Authorized_Maintenance) {
    toNormal();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_TRUE(modes.requestEnterMaintenance().ok());
    EXPECT_EQ(modes.mode(), Mode::Maintenance);
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Warning);
    EXPECT_FALSE(modes.dispensingAllowed());
    EXPECT_TRUE(modes.replenishmentAllowed());  // C1
    EXPECT_EQ(count(EventType::MAINTENANCE_ACTIVITY), 1);
}

TEST_F(ModeTest, SM1_Normal_EnterMaintenance_NotAuthorized_StayRejectLogAttempt) {
    toNormal();
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    const OpResult r = modes.requestEnterMaintenance();
    EXPECT_EQ(r.code, ResultCode::PermissionDenied);
    EXPECT_EQ(r.message, "You are not authorised to enter maintenance mode.");
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(log.all().back().type, EventType::ACCESS_DENIED);
}

TEST_F(ModeTest, SM1_Normal_ExitMaintenance_RejectedAndLogged) {
    toNormal();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(modes.requestExitMaintenance().code, ResultCode::InvalidEvent);
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(ModeTest, SM1_Normal_FaultDetected_Fault) {
    toNormal();
    faults.report(FaultType::IncompleteDispense, "D3", "x");
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_FALSE(modes.dispensingAllowed());
    EXPECT_FALSE(modes.replenishmentAllowed());
    const auto snap = cab.snapshot();
    EXPECT_EQ(snap.indicator, IndicatorLevel::Fault);
    EXPECT_NE(snap.indicatorText.find("did not complete"), std::string::npos);
}

TEST_F(ModeTest, SM1_Normal_FaultCleared_Ignore) {
    toNormal();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    const OpResult r = modes.requestClearFault(expected);
    EXPECT_EQ(r.message, "There is no active fault to clear.");
    EXPECT_EQ(modes.mode(), Mode::Normal);
}

TEST_F(ModeTest, SM1_Normal_PowerOff_Off) {
    toNormal();
    modes.powerOff();
    EXPECT_EQ(modes.mode(), Mode::Off);
}

TEST_F(ModeTest, SM1_Normal_CommunicationLoss_StaysNormalWithWarning) {
    toNormal();
    faults.reportCommunicationLoss();
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_TRUE(modes.dispensingAllowed());
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Warning);
    EXPECT_EQ(cab.snapshot().indicatorText, "Monitoring link down: cabinet working locally.");
}

// ---- Maintenance row ----------------------------------------------------------------------------
TEST_F(ModeTest, SM1_Maintenance_EnterMaintenance_Ignore) {
    toMaintenance();
    const auto before = log.lastId();
    EXPECT_FALSE(modes.requestEnterMaintenance().ok());
    EXPECT_EQ(modes.mode(), Mode::Maintenance);
    EXPECT_EQ(log.lastId(), before + 0);
}

TEST_F(ModeTest, SM1_Maintenance_ExitMaintenance_Authorized_Normal) {
    toMaintenance();
    EXPECT_TRUE(modes.requestExitMaintenance().ok());
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(count(EventType::MAINTENANCE_ACTIVITY), 2);
}

TEST_F(ModeTest, SM1_Maintenance_ExitMaintenance_NotAuthorized_RejectLog) {
    toMaintenance();
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    EXPECT_EQ(modes.requestExitMaintenance().code, ResultCode::PermissionDenied);
    EXPECT_EQ(modes.mode(), Mode::Maintenance);
    EXPECT_EQ(log.all().back().type, EventType::ACCESS_DENIED);
}

TEST_F(ModeTest, SM1_Maintenance_FaultDetected_Fault) {
    toMaintenance();
    faults.report(FaultType::SensorFailure, "D1", "x");
    EXPECT_EQ(modes.mode(), Mode::Fault);
}

TEST_F(ModeTest, SM1_Maintenance_PowerOff_Off) {
    toMaintenance();
    modes.powerOff();
    EXPECT_EQ(modes.mode(), Mode::Off);
}

TEST_F(ModeTest, SM1_Maintenance_StartupChecksPassed_Ignore) {
    toMaintenance();
    modes.completeStartup({});
    EXPECT_EQ(modes.mode(), Mode::Maintenance);
}

// ---- Fault row ----------------------------------------------------------------------------------
TEST_F(ModeTest, SM1_Fault_EnterMaintenance_RejectedAndLogged) {
    toFault();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(modes.requestEnterMaintenance().code, ResultCode::InvalidEvent);
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(ModeTest, SM1_Fault_ExitMaintenance_RejectedAndLogged) {
    toFault();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(modes.requestExitMaintenance().code, ResultCode::InvalidEvent);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(ModeTest, SM1_Fault_FaultDetected_StayButStillLogged_C6) {
    toFault();
    const int before = count(EventType::FAULT_DETECTED);
    const auto changesBefore = changes.size();
    faults.report(FaultType::SensorFailure, "D2", "x");
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_EQ(count(EventType::FAULT_DETECTED), before + 1);
    EXPECT_EQ(changes.size(), changesBefore);
}

TEST_F(ModeTest, SM1_Fault_FaultCleared_NotAuthorizedUser_StayLogDenial) {
    toFault();
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    EXPECT_EQ(modes.requestClearFault(expected).code, ResultCode::PermissionDenied);
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_EQ(log.all().back().type, EventType::ACCESS_DENIED);
}

TEST_F(ModeTest, SM1_Fault_FaultCleared_ConditionPersists_StayLogDenial) {
    toFault();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(modes.requestClearFault(expected).code, ResultCode::FaultStillPresent);
    EXPECT_EQ(modes.mode(), Mode::Fault);
    EXPECT_EQ(log.all().back().type, EventType::FAULT_CLEAR_DENIED);
}

TEST_F(ModeTest, SM1_Fault_FaultCleared_AuthorizedAndGone_Normal) {
    toFault();
    cab.o_setLock("D3", LockCommand::Locked);
    cab.simJamLock("D3", false);
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_TRUE(modes.requestClearFault(expected).ok());
    EXPECT_EQ(modes.mode(), Mode::Normal);
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Ok);
    expectInvMode1();
}

TEST_F(ModeTest, SM1_Fault_StartupChecksPassed_Ignore) {
    toFault();
    modes.completeStartup({});
    EXPECT_EQ(modes.mode(), Mode::Fault);
}

TEST_F(ModeTest, SM1_Fault_PowerOff_Off) {
    toFault();
    modes.powerOff();
    EXPECT_EQ(modes.mode(), Mode::Off);
}

// ---- Off (final state) --------------------------------------------------------------------------
TEST_F(ModeTest, SM1_Off_AnyEvent_Illegal_FailSafe) {
    toNormal();
    modes.powerOff();
    faults.report(FaultType::SensorFailure, "D1", "x");
    EXPECT_EQ(modes.mode(), Mode::Off);
    EXPECT_EQ(log.all().back().type, EventType::UNEXPECTED_EVENT);
    EXPECT_EQ(log.all().back().outcome, "ILLEGAL");
    EXPECT_FALSE(modes.dispensingAllowed());
    EXPECT_FALSE(modes.replenishmentAllowed());
}

TEST_F(ModeTest, SM1_Off_PowerOff_Ignore) {
    modes.powerOff();
    const auto before = log.lastId();
    modes.powerOff();
    EXPECT_EQ(log.lastId(), before);
}

// ---- cross-cutting ------------------------------------------------------------------------------
TEST_F(ModeTest, FR_LOG_003_EveryModeChangeLogged) {
    toMaintenance();
    modes.requestExitMaintenance();
    faults.report(FaultType::SensorFailure, "D1", "x");
    EXPECT_EQ(count(EventType::MODE_CHANGED), 4);  // Startup->Normal->Maintenance->Normal->Fault
    const auto rec = log.all();
    bool sawFaultChange = false;
    for (const auto& r : rec) {
        if (r.type == EventType::MODE_CHANGED && r.details.rfind("NORMAL->FAULT", 0) == 0) sawFaultChange = true;
    }
    EXPECT_TRUE(sawFaultChange);
}

TEST_F(ModeTest, INV_MODE_1_HoldsThroughAWholeCycle) {
    expectInvMode1();
    toFault();
    expectInvMode1();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    modes.requestClearFault(expected);
    expectInvMode1();
    cab.simJamLock("D3", false);
    cab.o_setLock("D3", LockCommand::Locked);
    modes.requestClearFault(expected);
    expectInvMode1();
    EXPECT_EQ(modes.mode(), Mode::Normal);
}

TEST_F(ModeTest, ModeDiag_R1_NurseDenied) {
    toNormal();
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    EXPECT_EQ(modes.runDiagnostics(expected).code, ResultCode::PermissionDenied);
}

TEST_F(ModeTest, ModeDiag_R2_NormalModeRefused) {
    toNormal();
    ASSERT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(modes.runDiagnostics(expected).code, ResultCode::ModeNotAllowed);
}

TEST_F(ModeTest, ModeDiag_R3_InMaintenance_RunsAndLogs) {
    std::vector<std::string> activities;
    modes.addMaintenanceListener([&](const std::string& a, const Badge&) { activities.push_back(a); });
    toMaintenance();
    EXPECT_TRUE(modes.runDiagnostics(expected).ok());
    EXPECT_EQ(log.all().back().type, EventType::MAINTENANCE_ACTIVITY);
    EXPECT_EQ(log.all().back().outcome, "PASSED");
    ASSERT_EQ(activities.size(), 2u);
    EXPECT_NE(activities[1].find("diagnostics"), std::string::npos);
}
