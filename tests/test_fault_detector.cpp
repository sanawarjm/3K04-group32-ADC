#include <gtest/gtest.h>

#include "adc/clock.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class FaultTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    Storage storage{dir.path()};
    EventLogger log{storage, clock};
    SimulatedCabinet cab{storage, cfg};
    FaultDetector faults{cab, log, clock, cfg};
    std::map<DrawerId, int> expected{{"D1", 20}, {"D2", 15}, {"D3", 10}, {"D4", 12}};
    std::vector<Fault> heard;

    void SetUp() override {
        faults.addListener([this](const Fault& f) { heard.push_back(f); });
    }
    void tick(const std::set<DrawerId>& authorized = {}) {
        clock.advance(cfg.tickPeriod);
        faults.check(authorized, expected);
    }
    int count(EventType t) const {
        int n = 0;
        for (const auto& r : log.all()) n += (r.type == t) ? 1 : 0;
        return n;
    }
};

TEST_F(FaultTest, FltCheck_R5_HealthyCabinet_NoFault) {
    for (int i = 0; i < 20; ++i) tick();
    EXPECT_TRUE(faults.activeFaults().empty());
    EXPECT_EQ(count(EventType::FAULT_DETECTED), 0);
}

TEST_F(FaultTest, FltCheck_R1_NoSensorReading_SensorFailure) {
    cab.simFailSensor("D2", true);
    tick();
    EXPECT_TRUE(faults.isActive(FaultType::SensorFailure, "D2"));
    EXPECT_TRUE(faults.hasCriticalFault());
    ASSERT_EQ(heard.size(), 1u);
    EXPECT_EQ(heard[0].drawer, "D2");
}

TEST_F(FaultTest, FltCheck_R1_AppliesEvenToAuthorizedDrawer) {
    cab.simFailSensor("D3", true);
    tick({"D3"});
    EXPECT_TRUE(faults.isActive(FaultType::SensorFailure, "D3"));
}

TEST_F(FaultTest, FltCheck_R2_OpenWithoutAuthorization_UnauthorizedAccess) {
    cab.simForceOpen("D1");
    tick();
    EXPECT_TRUE(faults.isActive(FaultType::UnauthorizedAccess, "D1"));
}

TEST_F(FaultTest, FltCheck_R2_OpenWithAuthorization_NoFault) {
    cab.o_setLock("D3", LockCommand::Unlocked);
    cab.simOpen("D3");
    tick({"D3"});
    EXPECT_TRUE(faults.activeFaults().empty());
}

TEST_F(FaultTest, FltCheck_R3_UnlockedPastLockConfirmTimeout_StorageNotClosed) {
    cab.o_setLock("D3", LockCommand::Unlocked);  // closed but unlocked, no session
    tick();
    clock.advance(cfg.lockConfirmTimeout - cfg.tickPeriod - cfg.tickPeriod);
    tick();
    EXPECT_FALSE(faults.isActive(FaultType::StorageNotClosed, "D3"));  // just under 2 s
    tick();
    EXPECT_TRUE(faults.isActive(FaultType::StorageNotClosed, "D3"));   // reached 2 s
}

TEST_F(FaultTest, FltCheck_R3_RelockInTime_NoFault) {
    cab.o_setLock("D3", LockCommand::Unlocked);
    tick();
    cab.o_setLock("D3", LockCommand::Locked);
    for (int i = 0; i < 20; ++i) tick();
    EXPECT_TRUE(faults.activeFaults().empty());
}

TEST_F(FaultTest, FltCheck_R4_IdleDrawerCountDiscrepancy_IncompleteDispense) {
    expected["D4"] = 11;  // records disagree with the sensed 12
    tick();
    EXPECT_TRUE(faults.isActive(FaultType::IncompleteDispense, "D4"));
}

TEST_F(FaultTest, FltCheck_R4_AuthorizedDrawerNotChecked) {
    expected["D4"] = 11;
    tick({"D4"});
    EXPECT_FALSE(faults.isActive(FaultType::IncompleteDispense, "D4"));
}

TEST_F(FaultTest, FltCheck_R6_LoggerUnhealthy_StorageFailure) {
    storage.simulateWriteFailure(true);
    log.record(EventType::SYSTEM_STARTUP, "ADC", "", "");
    tick();
    EXPECT_TRUE(faults.isActive(FaultType::StorageFailure, ""));
}

TEST_F(FaultTest, ReportIsIdempotentWhileActive) {
    EXPECT_TRUE(faults.report(FaultType::IncompleteDispense, "D3", "requested=2 removed=1"));
    EXPECT_FALSE(faults.report(FaultType::IncompleteDispense, "D3", "again"));
    EXPECT_EQ(faults.activeFaults().size(), 1u);
    EXPECT_EQ(count(EventType::FAULT_DETECTED), 1);
    EXPECT_EQ(heard.size(), 1u);
    EXPECT_TRUE(faults.report(FaultType::IncompleteDispense, "D2", "other drawer"));
}

TEST_F(FaultTest, FR_LOG_002_FaultLogged) {
    faults.report(FaultType::StorageNotClosed, "D3", "lock not engaged");
    const auto rec = log.all().back();
    EXPECT_EQ(rec.type, EventType::FAULT_DETECTED);
    EXPECT_EQ(rec.outcome, "StorageNotClosed");
    EXPECT_NE(rec.details.find("drawer=D3"), std::string::npos);
}

TEST_F(FaultTest, NFR_USB_002_PlainMessages) {
    faults.report(FaultType::StorageNotClosed, "D3", "x");
    const Fault f = faults.activeFaults().at(0);
    EXPECT_EQ(f.message, "A drawer did not lock (drawer D3). Dispensing is stopped. Call a technician.");
    for (int t = 0; t <= static_cast<int>(FaultType::CommunicationLoss); ++t)
        EXPECT_FALSE(FaultDetector::messageFor(static_cast<FaultType>(t), "D1").empty());
}

TEST_F(FaultTest, SelfTest_Pass) {
    EXPECT_TRUE(faults.selfTest(expected).empty());
    EXPECT_TRUE(faults.activeFaults().empty());
}

TEST_F(FaultTest, SelfTest_Fail_RaisesStartupCheckFailedWithoutListeners) {
    cab.simForceOpen("D2");
    cab.simFailSensor("D4", true);
    const auto failed = faults.selfTest(expected);
    ASSERT_EQ(failed.size(), 2u);
    EXPECT_EQ(failed[0].type, FaultType::StartupCheckFailed);
    EXPECT_TRUE(faults.isActive(FaultType::StartupCheckFailed, "D2"));
    EXPECT_TRUE(faults.isActive(FaultType::StartupCheckFailed, "D4"));
    EXPECT_TRUE(heard.empty());
    EXPECT_EQ(count(EventType::FAULT_DETECTED), 2);
}

TEST_F(FaultTest, SelfTest_CountMismatchFails) {
    expected["D1"] = 19;
    EXPECT_EQ(faults.selfTest(expected).size(), 1u);
}

TEST_F(FaultTest, Diagnose_ReportsFindingsWithoutRaising) {
    cab.simJamLock("D1", true);
    cab.o_setLock("D1", LockCommand::Unlocked);
    const auto findings = faults.diagnose(expected);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_NE(findings[0].find("D1"), std::string::npos);
    EXPECT_TRUE(faults.activeFaults().empty());
}

TEST_F(FaultTest, TryClear_DeniedWhileConditionPersists_SucceedsAfterRepair) {
    cab.o_setLock("D3", LockCommand::Unlocked);
    cab.simJamLock("D3", true);
    cab.o_setLock("D3", LockCommand::Locked);  // jammed: stays disengaged
    faults.report(FaultType::StorageNotClosed, "D3", "x");
    OpResult r = faults.tryClear("T300", expected);
    EXPECT_EQ(r.code, ResultCode::FaultStillPresent);
    EXPECT_TRUE(faults.hasCriticalFault());
    EXPECT_EQ(log.all().back().type, EventType::FAULT_CLEAR_DENIED);
    cab.simJamLock("D3", false);  // repair -> engages
    r = faults.tryClear("T300", expected);
    EXPECT_TRUE(r.ok());
    EXPECT_FALSE(faults.hasCriticalFault());
    EXPECT_EQ(log.all().back().type, EventType::FAULT_CLEARED);
    EXPECT_EQ(log.all().back().user, "T300");
}

TEST_F(FaultTest, TryClear_SensorFailureAfterRepair) {
    cab.simFailSensor("D2", true);
    tick();
    EXPECT_FALSE(faults.tryClear("T300", expected).ok());
    cab.simFailSensor("D2", false);
    EXPECT_TRUE(faults.tryClear("T300", expected).ok());
}

TEST_F(FaultTest, TryClear_StorageFailureAfterStorageRecovers) {
    storage.simulateWriteFailure(true);
    log.record(EventType::SYSTEM_STARTUP, "ADC", "", "");
    tick();
    EXPECT_FALSE(faults.tryClear("T300", expected).ok());
    storage.simulateWriteFailure(false);
    EXPECT_TRUE(faults.tryClear("T300", expected).ok());
    EXPECT_TRUE(log.healthy());
}

TEST_F(FaultTest, INV_FLT_3_CommunicationLossNotCritical) {
    EXPECT_FALSE(FaultDetector::isCritical(FaultType::CommunicationLoss));
    EXPECT_TRUE(FaultDetector::isCritical(FaultType::StorageNotClosed));
    faults.reportCommunicationLoss();
    EXPECT_TRUE(faults.isActive(FaultType::CommunicationLoss, ""));
    EXPECT_FALSE(faults.hasCriticalFault());
    ASSERT_EQ(heard.size(), 1u);
    EXPECT_FALSE(heard[0].critical);
    EXPECT_TRUE(faults.tryClear("T300", expected).ok());          // nothing critical to clear
    EXPECT_TRUE(faults.isActive(FaultType::CommunicationLoss, ""));  // not cleared by tryClear
    faults.clearCommunicationLoss();
    EXPECT_FALSE(faults.isActive(FaultType::CommunicationLoss, ""));
    EXPECT_EQ(log.all().back().type, EventType::FAULT_CLEARED);
}
