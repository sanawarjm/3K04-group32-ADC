#include <gtest/gtest.h>

#include <memory>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/event_logger.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class InventoryTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    Storage storage{dir.path()};
    EventLogger log{storage, clock};
    AccessControlManager access{log, clock, cfg};
    std::unique_ptr<InventoryManager> inv = std::make_unique<InventoryManager>(storage, log, access, cfg);
};

TEST_F(InventoryTest, FR_INV_001_SeededLibraryAndLookups) {
    EXPECT_TRUE(inv->hasMedication("M03"));
    EXPECT_EQ(inv->quantityOf("M01"), 20);
    EXPECT_EQ(inv->quantityOf("M02"), 15);
    EXPECT_EQ(inv->quantityOf("M03"), 10);
    EXPECT_EQ(inv->quantityOf("M04"), 12);
    EXPECT_EQ(inv->locationOf("M03"), DrawerId("D3"));  // FR-DISP-002
    EXPECT_EQ(inv->medicationAt("D4"), MedicationId("M04"));
    EXPECT_EQ(inv->nameOf("M03"), "Morphine 2 mg/mL injection");
    EXPECT_EQ(inv->medications().size(), 4u);
    EXPECT_EQ(inv->expectedCounts().at("D2"), 15);
}

TEST_F(InventoryTest, UnknownMedication) {
    EXPECT_FALSE(inv->hasMedication("M99"));
    EXPECT_EQ(inv->quantityOf("M99"), 0);
    EXPECT_FALSE(inv->locationOf("M99").has_value());
    EXPECT_EQ(inv->canDispense("M99", 1).code, ResultCode::UnknownMedication);
    EXPECT_EQ(inv->recordRemoval("M99", 1, "N100", "x").code, ResultCode::UnknownMedication);
}

TEST_F(InventoryTest, CanDispenseRules) {
    EXPECT_TRUE(inv->canDispense("M03", 10).ok());
    EXPECT_EQ(inv->canDispense("M03", 0).code, ResultCode::InvalidQuantity);
    EXPECT_EQ(inv->canDispense("M03", -1).code, ResultCode::InvalidQuantity);
    EXPECT_EQ(inv->canDispense("M03", 11).code, ResultCode::InsufficientStock);
}

TEST_F(InventoryTest, QuantityZeroOrNegativeChangeRejected) {
    EXPECT_EQ(inv->recordRemoval("M03", 0, "N100", "x").code, ResultCode::InvalidQuantity);
    EXPECT_EQ(inv->recordAddition("M03", -2, "P200", "x").code, ResultCode::InvalidQuantity);
    EXPECT_EQ(inv->quantityOf("M03"), 10);
}

TEST_F(InventoryTest, INV_INV_1_NeverNegative) {
    EXPECT_EQ(inv->recordRemoval("M03", 11, "N100", "x").code, ResultCode::InvalidQuantity);
    EXPECT_EQ(inv->quantityOf("M03"), 10);
    EXPECT_TRUE(inv->recordRemoval("M03", 10, "N100", "x").ok());
    EXPECT_EQ(inv->quantityOf("M03"), 0);
    EXPECT_FALSE(inv->recordRemoval("M03", 1, "N100", "x").ok());
}

TEST_F(InventoryTest, FR_INV_002_RemovalAndAddition) {
    EXPECT_TRUE(inv->recordRemoval("M03", 2, "N100", "dispense").ok());
    EXPECT_EQ(inv->quantityOf("M03"), 8);
    EXPECT_TRUE(inv->recordAddition("M04", 8, "P200", "replenish").ok());
    EXPECT_EQ(inv->quantityOf("M04"), 20);
}

TEST_F(InventoryTest, FR_INV_003_ReplenishDeniedForNurse) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    const OpResult r = inv->replenishAuthorized();
    EXPECT_EQ(r.code, ResultCode::PermissionDenied);
    EXPECT_EQ(log.all().back().type, EventType::ACCESS_DENIED);
    ASSERT_TRUE(access.authenticate("P200", "2345").ok());
    EXPECT_TRUE(inv->replenishAuthorized().ok());
}

TEST_F(InventoryTest, FR_INV_004_ChangeLogsInventoryChanged) {
    ASSERT_TRUE(inv->recordRemoval("M03", 2, "N100", "dispense").ok());
    const auto rec = log.all().back();
    EXPECT_EQ(rec.type, EventType::INVENTORY_CHANGED);
    EXPECT_EQ(rec.user, "N100");
    EXPECT_EQ(rec.medication, "M03");
    EXPECT_EQ(rec.quantity, 2);
    EXPECT_EQ(rec.outcome, "REMOVED");
    EXPECT_NE(rec.details.find("10->8"), std::string::npos);
}

TEST_F(InventoryTest, NFR_REL_001_PersistsAcrossRestart) {
    ASSERT_TRUE(inv->recordRemoval("M03", 2, "N100", "dispense").ok());
    inv.reset();
    cfg.seedInventory.clear();  // a second run must not re-seed
    Storage again(dir.path());
    InventoryManager reloaded(again, log, access, cfg);
    EXPECT_EQ(reloaded.quantityOf("M03"), 8);
    EXPECT_EQ(reloaded.medications().size(), 4u);
}

TEST_F(InventoryTest, StorageFailureRollsBackAndReturnsStorageError) {
    int notified = 0;
    inv->addChangeListener([&](const Medication&) { ++notified; });
    const auto before = log.lastId();
    storage.simulateWriteFailure(true);
    const OpResult r = inv->recordRemoval("M03", 2, "N100", "dispense");
    EXPECT_EQ(r.code, ResultCode::StorageError);
    EXPECT_EQ(inv->quantityOf("M03"), 10);  // rolled back
    EXPECT_EQ(notified, 0);
    EXPECT_EQ(log.lastId(), before);  // nothing reported
    storage.simulateWriteFailure(false);
    Storage reread(dir.path());
    InventoryManager reloaded(reread, log, access, cfg);
    EXPECT_EQ(reloaded.quantityOf("M03"), 10);
}

TEST_F(InventoryTest, ListenerFiredWithNewRecord) {
    Medication seen;
    int calls = 0;
    inv->addChangeListener([&](const Medication& m) {
        seen = m;
        ++calls;
    });
    ASSERT_TRUE(inv->recordAddition("M01", 5, "P200", "replenish").ok());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen.id, "M01");
    EXPECT_EQ(seen.quantity, 25);
}

TEST_F(InventoryTest, MalformedRowsSkippedOnLoad) {
    inv.reset();
    ASSERT_TRUE(storage.saveSnapshot("inventory", {{"M01", "A", "D1", "5"}, {"M02", "bad"}, {"M03", "C", "D3", "-4"}}));
    InventoryManager reloaded(storage, log, access, cfg);
    EXPECT_EQ(reloaded.medications().size(), 1u);
    EXPECT_EQ(reloaded.quantityOf("M01"), 5);
}
