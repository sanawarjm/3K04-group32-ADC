#include <gtest/gtest.h>

#include <set>
#include <string>

#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/types.hpp"

using namespace adc;

namespace {

// Round-trips every enumerator from first to last, and checks spellings are unique.
template <typename E>
void expectRoundTrip(E first, E last) {
    std::set<std::string> seen;
    for (int i = static_cast<int>(first); i <= static_cast<int>(last); ++i) {
        const E value = static_cast<E>(i);
        const std::string text = toString(value);
        EXPECT_NE(text, "UNKNOWN");
        EXPECT_TRUE(seen.insert(text).second) << "duplicate spelling " << text;
        E back = first;
        ASSERT_TRUE(fromString(text, back)) << text;
        EXPECT_EQ(static_cast<int>(back), i);
    }
    E untouched = last;
    EXPECT_FALSE(fromString("no-such-value", untouched));
    EXPECT_EQ(untouched, last);
}

}  // namespace

TEST(Types, ToStringRoundTrips_AllEnums) {
    expectRoundTrip(Role::Nurse, Role::Technician);
    expectRoundTrip(Permission::Dispense, Permission::ModifyAuditRecords);
    expectRoundTrip(Mode::Startup, Mode::Off);
    expectRoundTrip(ModeEvent::StartupChecksPassed, ModeEvent::PowerOff);
    expectRoundTrip(DispenseState::Idle, DispenseState::Interrupted);
    expectRoundTrip(DispenseEvent::DispenseRequest, DispenseEvent::FaultDetected);
    expectRoundTrip(LinkState::Connecting, LinkState::Off);
    expectRoundTrip(LinkEvent::ConnectionEstablished, LinkEvent::PowerOff);
    expectRoundTrip(FaultType::StorageNotClosed, FaultType::CommunicationLoss);
    expectRoundTrip(EventType::SYSTEM_STARTUP, EventType::CONFIG_UPDATED);
    expectRoundTrip(ResultCode::Ok, ResultCode::InvalidInput);
    expectRoundTrip(LockCommand::Locked, LockCommand::Unlocked);
    expectRoundTrip(IndicatorLevel::Ok, IndicatorLevel::Fault);
}

TEST(Types, D1Spellings) {
    EXPECT_STREQ(toString(Mode::Normal), "NORMAL");
    EXPECT_STREQ(toString(LinkState::Disconnected), "DISCONNECTED");
    EXPECT_STREQ(toString(DispenseState::Completed), "COMPLETED");
    EXPECT_STREQ(toString(EventType::DISPENSE_OUTCOME), "DISPENSE_OUTCOME");
    EXPECT_STREQ(toString(DispenseEvent::CompartmentClosed), "compartmentClosed");
}

TEST(Types, TerminalStatesAreTheFiveD1Ones) {
    EXPECT_FALSE(isTerminal(DispenseState::Idle));
    EXPECT_FALSE(isTerminal(DispenseState::AuthorizationPending));
    EXPECT_FALSE(isTerminal(DispenseState::AwaitingRetrieval));
    EXPECT_FALSE(isTerminal(DispenseState::AwaitingClosure));
    EXPECT_TRUE(isTerminal(DispenseState::Rejected));
    EXPECT_TRUE(isTerminal(DispenseState::Cancelled));
    EXPECT_TRUE(isTerminal(DispenseState::TimedOut));
    EXPECT_TRUE(isTerminal(DispenseState::Completed));
    EXPECT_TRUE(isTerminal(DispenseState::Interrupted));
}

TEST(Config, DefaultsMatchBrief) {
    const AdcConfig cfg;
    EXPECT_EQ(cfg.cabinetId, "ADC-01");
    EXPECT_EQ(cfg.sessionTimeout, Millis(60000));
    EXPECT_EQ(cfg.retrievalTimeout, Millis(30000));
    EXPECT_EQ(cfg.closureTimeout, Millis(20000));
    EXPECT_EQ(cfg.lockConfirmTimeout, Millis(2000));
    EXPECT_EQ(cfg.commLossTimeout, Millis(3000));
    EXPECT_EQ(cfg.heartbeatPeriod, Millis(1000));
    EXPECT_EQ(cfg.tickPeriod, Millis(200));
    EXPECT_EQ(cfg.dataDir, "data");
    ASSERT_EQ(cfg.drawers.size(), 4u);
    ASSERT_EQ(cfg.seedInventory.size(), 4u);
    EXPECT_EQ(cfg.seedInventory[2].id, "M03");
    EXPECT_EQ(cfg.seedInventory[2].drawer, "D3");
    EXPECT_EQ(cfg.seedInventory[2].quantity, 10);
}

TEST(Config, INV_CFG_1_LossDetectedWithinFiveSeconds) {
    const AdcConfig cfg;
    EXPECT_LT(cfg.heartbeatPeriod, cfg.commLossTimeout);
    EXPECT_LT(cfg.commLossTimeout + cfg.tickPeriod, Millis(5000));  // NFR-PRF-003
}

TEST(Clock, ManualClockAdvancesAndFormatsIso) {
    ManualClock clock;
    EXPECT_EQ(clock.now(), Millis(0));
    EXPECT_EQ(clock.timestamp(), "1970-01-01T00:00:00.000Z");
    clock.advance(Millis(61250));
    EXPECT_EQ(clock.now(), Millis(61250));
    EXPECT_EQ(clock.timestamp(), "1970-01-01T00:01:01.250Z");
    clock.advance(Millis(-5));  // ignored: INV-CLK-1
    EXPECT_EQ(clock.now(), Millis(61250));
}

TEST(Clock, SystemClockIsMonotonicAndIso) {
    SystemClock clock;
    const auto a = clock.now();
    const auto b = clock.now();
    EXPECT_LE(a, b);
    const std::string ts = clock.timestamp();
    ASSERT_EQ(ts.size(), std::string("2026-11-03T14:05:09.120Z").size());
    EXPECT_EQ(ts[4], '-');
    EXPECT_EQ(ts[10], 'T');
    EXPECT_EQ(ts.back(), 'Z');
}
