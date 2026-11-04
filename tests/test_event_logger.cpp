#include <gtest/gtest.h>

#include "adc/clock.hpp"
#include "adc/event_logger.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class EventLoggerTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    ManualClock clock;
};

TEST_F(EventLoggerTest, RecordHasAllFields) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    clock.advance(Millis(1500));
    log.recordItem(EventType::INVENTORY_CHANGED, "N100", "M03", 2, "REMOVED", "10->8");
    const auto rec = log.all().at(0);
    EXPECT_EQ(rec.id, 1);
    EXPECT_EQ(rec.timestamp, "1970-01-01T00:00:01.500Z");
    EXPECT_EQ(rec.type, EventType::INVENTORY_CHANGED);
    EXPECT_EQ(rec.user, "N100");
    EXPECT_EQ(rec.medication, "M03");
    EXPECT_EQ(rec.quantity, 2);
    EXPECT_EQ(rec.outcome, "REMOVED");
    EXPECT_EQ(rec.details, "10->8");
}

TEST_F(EventLoggerTest, INV_LOG_1_IdsAreOneToNWithoutGaps) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    EXPECT_EQ(log.lastId(), 0);
    for (int i = 0; i < 10; ++i) log.record(EventType::AUTH_FAILURE, "N100", "FAILED", "");
    const auto all = log.all();
    ASSERT_EQ(all.size(), 10u);
    for (std::size_t i = 0; i < all.size(); ++i) EXPECT_EQ(all[i].id, static_cast<std::int64_t>(i + 1));
    EXPECT_EQ(log.lastId(), 10);
}

TEST_F(EventLoggerTest, NFR_AUD_003_Chronological) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    for (int i = 0; i < 5; ++i) {
        clock.advance(Millis(250));
        log.record(EventType::MODE_CHANGED, "ADC", "", "");
    }
    const auto all = log.all();
    for (std::size_t i = 1; i < all.size(); ++i) {
        EXPECT_LT(all[i - 1].id, all[i].id);
        EXPECT_LT(all[i - 1].timestamp, all[i].timestamp);
    }
}

TEST_F(EventLoggerTest, NFR_AUD_002_DispenseRecordHasUserMedQtyTimeOutcome) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    log.recordDispense("N100", "M03", 2, 2, DispenseState::Completed, "drawer closed");
    const auto rec = log.all().at(0);
    EXPECT_EQ(rec.type, EventType::DISPENSE_OUTCOME);
    EXPECT_EQ(rec.user, "N100");
    EXPECT_EQ(rec.medication, "M03");
    EXPECT_EQ(rec.quantity, 2);
    EXPECT_FALSE(rec.timestamp.empty());
    EXPECT_EQ(rec.outcome, "COMPLETED");
    EXPECT_NE(rec.details.find("requested=2"), std::string::npos);
}

TEST_F(EventLoggerTest, ReplenishRecord) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    log.recordReplenish("P200", "M04", 8, DispenseState::Completed, "drawer closed");
    const auto rec = log.all().at(0);
    EXPECT_EQ(rec.type, EventType::REPLENISH_OUTCOME);
    EXPECT_EQ(rec.quantity, 8);
}

TEST_F(EventLoggerTest, NFR_AUD_005_ReturnedCopiesCannotChangeTheLog) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    log.record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "");
    auto copy = log.all();
    copy[0].user = "TAMPERED";
    copy.clear();
    auto recent = log.recent(1);
    recent[0].outcome = "TAMPERED";
    EXPECT_EQ(log.all().at(0).user, "N100");
    EXPECT_EQ(log.all().at(0).outcome, "SUCCESS");
}

TEST_F(EventLoggerTest, AfterAndRecent) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    for (int i = 0; i < 6; ++i) log.record(EventType::MODE_CHANGED, "ADC", "", std::to_string(i));
    const auto a = log.after(2, 3);
    ASSERT_EQ(a.size(), 3u);
    EXPECT_EQ(a[0].id, 3);
    EXPECT_EQ(a[2].id, 5);
    EXPECT_TRUE(log.after(6, 10).empty());
    const auto r = log.recent(2);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].id, 5);
    EXPECT_EQ(r[1].id, 6);
    EXPECT_EQ(log.recent(100).size(), 6u);
}

TEST_F(EventLoggerTest, NFR_REL_001_ReloadContinuesIds) {
    {
        Storage s(dir.path());
        EventLogger log(s, clock);
        log.record(EventType::SYSTEM_STARTUP, "ADC", "", "");
        log.record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "");
    }
    Storage s(dir.path());
    EventLogger log(s, clock);
    ASSERT_EQ(log.all().size(), 2u);
    EXPECT_EQ(log.all()[1].user, "N100");
    EXPECT_EQ(log.record(EventType::SYSTEM_STARTUP, "ADC", "", ""), 3);
}

TEST_F(EventLoggerTest, NFR_REL_002_StorageFailureUnhealthyNothingLostRetried) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    log.record(EventType::SYSTEM_STARTUP, "ADC", "", "");
    s.simulateWriteFailure(true);
    log.record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "");
    log.record(EventType::AUTH_FAILURE, "N101", "FAILED", "");
    EXPECT_FALSE(log.healthy());
    EXPECT_EQ(log.unsavedCount(), 2);
    EXPECT_EQ(log.all().size(), 3u);  // nothing dropped
    EXPECT_FALSE(log.retryPending());
    s.simulateWriteFailure(false);
    log.record(EventType::SESSION_ENDED, "N101", "", "");  // retried before this record
    EXPECT_TRUE(log.healthy());
    EXPECT_EQ(log.unsavedCount(), 0);
    Storage reread(dir.path());
    EventLogger reloaded(reread, clock);
    const auto all = reloaded.all();
    ASSERT_EQ(all.size(), 4u);
    for (std::size_t i = 0; i < all.size(); ++i) EXPECT_EQ(all[i].id, static_cast<std::int64_t>(i + 1));
}

TEST_F(EventLoggerTest, RetryPendingRecoversWithoutNewRecord) {
    Storage s(dir.path());
    EventLogger log(s, clock);
    s.simulateWriteFailure(true);
    log.record(EventType::SYSTEM_STARTUP, "ADC", "", "");
    EXPECT_FALSE(log.healthy());
    s.simulateWriteFailure(false);
    EXPECT_TRUE(log.retryPending());
    EXPECT_TRUE(log.healthy());
}

TEST_F(EventLoggerTest, NoPinFieldExists) {
    // The record has no PIN field at all; callers log the badge only (R6). Checked end to end in
    // test_access_control.cpp and test_scenarios.cpp.
    Storage s(dir.path());
    EventLogger log(s, clock);
    log.record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "role=Nurse");
    const auto r = log.all().at(0);
    for (const std::string& field : {r.timestamp, r.user, r.medication, r.outcome, r.details})
        EXPECT_EQ(field.find("1234"), std::string::npos);
}
