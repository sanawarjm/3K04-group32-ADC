#include <gtest/gtest.h>

#include "adc/hardware_interface.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class CabinetTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    Storage storage{dir.path()};
    SimulatedCabinet cab{storage, cfg};
    HardwareInterface& hw = cab;  // ADC logic sees only this
};

TEST_F(CabinetTest, FirstRunAllDrawersClosedLockedSeeded) {
    ASSERT_EQ(hw.drawers(), (std::vector<DrawerId>{"D1", "D2", "D3", "D4"}));
    EXPECT_EQ(hw.i_drawerOpen("D3"), false);
    EXPECT_EQ(hw.i_lockEngaged("D3"), true);
    EXPECT_EQ(hw.i_itemCount("D1"), 20);
    EXPECT_EQ(hw.i_itemCount("D3"), 10);
    EXPECT_EQ(hw.i_itemCount("D4"), 12);
}

TEST_F(CabinetTest, UnknownDrawerHasNoReading) {
    EXPECT_FALSE(hw.i_drawerOpen("D9").has_value());
    EXPECT_FALSE(hw.i_itemCount("D9").has_value());
    EXPECT_FALSE(cab.simOpen("D9").ok());
    hw.o_setLock("D9", LockCommand::Unlocked);  // ignored, no crash
}

TEST_F(CabinetTest, NAT_1_LockedDrawerDoesNotOpen) {
    EXPECT_FALSE(cab.simOpen("D3").ok());
    EXPECT_EQ(hw.i_drawerOpen("D3"), false);
    hw.o_setLock("D3", LockCommand::Unlocked);
    EXPECT_EQ(hw.i_lockEngaged("D3"), false);
    EXPECT_TRUE(cab.simOpen("D3").ok());
    EXPECT_EQ(hw.i_drawerOpen("D3"), true);
}

TEST_F(CabinetTest, NAT_1_ForceOpenIsTamper) {
    EXPECT_TRUE(cab.simForceOpen("D2").ok());
    EXPECT_EQ(hw.i_drawerOpen("D2"), true);
    EXPECT_EQ(hw.i_lockEngaged("D2"), false);
}

TEST_F(CabinetTest, NAT_2_CountChangesOnlyWhileOpen) {
    EXPECT_FALSE(cab.simRemove("D3", 1).ok());
    EXPECT_FALSE(cab.simAdd("D3", 1).ok());
    EXPECT_EQ(hw.i_itemCount("D3"), 10);
    hw.o_setLock("D3", LockCommand::Unlocked);
    ASSERT_TRUE(cab.simOpen("D3").ok());
    EXPECT_TRUE(cab.simRemove("D3", 2).ok());
    EXPECT_TRUE(cab.simAdd("D3", 1).ok());
    EXPECT_EQ(hw.i_itemCount("D3"), 9);
}

TEST_F(CabinetTest, NAT_3_CountNeverBelowZero) {
    hw.o_setLock("D3", LockCommand::Unlocked);
    ASSERT_TRUE(cab.simOpen("D3").ok());
    EXPECT_FALSE(cab.simRemove("D3", 11).ok());
    EXPECT_FALSE(cab.simRemove("D3", 0).ok());
    EXPECT_TRUE(cab.simRemove("D3", 10).ok());
    EXPECT_EQ(hw.i_itemCount("D3"), 0);
    EXPECT_FALSE(cab.simRemove("D3", 1).ok());
}

TEST_F(CabinetTest, NAT_4_LockEngagesOnlyWhenClosedAndNotJammed) {
    hw.o_setLock("D3", LockCommand::Unlocked);
    ASSERT_TRUE(cab.simOpen("D3").ok());
    hw.o_setLock("D3", LockCommand::Locked);
    EXPECT_EQ(hw.i_lockEngaged("D3"), false);  // open: cannot engage yet
    ASSERT_TRUE(cab.simClose("D3").ok());
    EXPECT_EQ(hw.i_lockEngaged("D3"), true);   // engages on close
}

TEST_F(CabinetTest, NAT_4_JammedLockDoesNotEngageUntilRepaired) {
    hw.o_setLock("D3", LockCommand::Unlocked);
    ASSERT_TRUE(cab.simJamLock("D3", true).ok());
    ASSERT_TRUE(cab.simOpen("D3").ok());
    ASSERT_TRUE(cab.simClose("D3").ok());
    hw.o_setLock("D3", LockCommand::Locked);
    EXPECT_EQ(hw.i_lockEngaged("D3"), false);
    ASSERT_TRUE(cab.simJamLock("D3", false).ok());
    EXPECT_EQ(hw.i_lockEngaged("D3"), true);
}

TEST_F(CabinetTest, SensorFailureGivesNoReading) {
    ASSERT_TRUE(cab.simFailSensor("D1", true).ok());
    EXPECT_FALSE(hw.i_drawerOpen("D1").has_value());
    EXPECT_FALSE(hw.i_lockEngaged("D1").has_value());
    EXPECT_FALSE(hw.i_itemCount("D1").has_value());
    EXPECT_TRUE(hw.i_itemCount("D2").has_value());
    ASSERT_TRUE(cab.simFailSensor("D1", false).ok());
    EXPECT_EQ(hw.i_itemCount("D1"), 20);
}

TEST_F(CabinetTest, NFR_USB_003_IndicatorNeverColourAlone) {
    for (IndicatorLevel level : {IndicatorLevel::Ok, IndicatorLevel::Warning, IndicatorLevel::Fault}) {
        const IndicatorSignal s = signalFor(level);
        EXPECT_STREQ(s.label, toString(level));
        EXPECT_NE(std::string(s.light), "");
    }
    EXPECT_FALSE(signalFor(IndicatorLevel::Ok).buzzer);
    EXPECT_FALSE(signalFor(IndicatorLevel::Warning).buzzer);
    EXPECT_TRUE(signalFor(IndicatorLevel::Fault).buzzer);
    EXPECT_STREQ(signalFor(IndicatorLevel::Fault).light, "fast flashing red");
    hw.o_setIndicator(IndicatorLevel::Fault, "A drawer did not lock.");
    EXPECT_EQ(cab.snapshot().indicator, IndicatorLevel::Fault);
    EXPECT_EQ(cab.snapshot().indicatorText, "A drawer did not lock.");
}

TEST_F(CabinetTest, PhysicalStatePersistsAcrossRestart) {
    hw.o_setLock("D2", LockCommand::Unlocked);
    ASSERT_TRUE(cab.simOpen("D2").ok());
    ASSERT_TRUE(cab.simRemove("D2", 3).ok());
    ASSERT_TRUE(cab.simJamLock("D4", true).ok());
    ASSERT_TRUE(cab.simFailSensor("D1", true).ok());
    Storage again(dir.path());
    SimulatedCabinet restarted(again, cfg);
    const auto snap = restarted.snapshot();
    ASSERT_EQ(snap.drawers.size(), 4u);
    EXPECT_FALSE(restarted.i_itemCount("D1").has_value());
    EXPECT_EQ(restarted.i_drawerOpen("D2"), true);
    EXPECT_EQ(restarted.i_itemCount("D2"), 12);
    EXPECT_TRUE(snap.drawers[3].lockJammed);
}
