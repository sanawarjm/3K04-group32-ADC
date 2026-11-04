#include <gtest/gtest.h>

#include <memory>
#include <set>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/communication_manager.hpp"
#include "adc/dispensing_controller.hpp"
#include "adc/event_logger.hpp"
#include "adc/fake_monitor.hpp"
#include "adc/fault_detector.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class CommTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    FakeMonitor monitor;
    std::unique_ptr<Storage> storage = std::make_unique<Storage>(dir.path());
    std::unique_ptr<EventLogger> log = std::make_unique<EventLogger>(*storage, clock);
    std::unique_ptr<CommunicationManager> comm;
    std::vector<std::pair<LinkState, LinkState>> changes;

    void SetUp() override { build(); }
    void build() {
        comm = std::make_unique<CommunicationManager>(monitor, *log, *storage, clock, cfg);
        comm->addLinkListener([this](LinkState a, LinkState b) { changes.push_back({a, b}); });
    }
    void restart() {
        comm.reset();
        log.reset();
        storage = std::make_unique<Storage>(dir.path());
        log = std::make_unique<EventLogger>(*storage, clock);
        build();
    }
    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            clock.advance(cfg.tickPeriod);
            comm->tick();
        }
    }
    LinkState st() const { return comm->state(); }
    void toConnected() {
        step();
        ASSERT_EQ(st(), LinkState::Connected);
    }
    void toDisconnected() {
        toConnected();
        monitor.setOnline(false);
        step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod) + 1);
        ASSERT_EQ(st(), LinkState::Disconnected);
    }
    int receivedOfType(const std::string& type) const {
        int n = 0;
        for (const auto& m : monitor.received()) n += m.type == type ? 1 : 0;
        return n;
    }
};

// ---- SM3 table ----------------------------------------------------------------------------------
TEST_F(CommTest, SM3_Table_MatchesD1) {
    using E = LinkEvent;
    auto cell = [](LinkState s, E e) { return CommunicationManager::transitionFor(s, e); };
    EXPECT_EQ(cell(LinkState::Connecting, E::ConnectionEstablished).target, LinkState::Connected);
    EXPECT_EQ(cell(LinkState::Connecting, E::CommunicationLost).target, LinkState::Disconnected);  // C4
    EXPECT_EQ(cell(LinkState::Connected, E::ConnectionEstablished).kind, "Ignore");
    EXPECT_EQ(cell(LinkState::Connected, E::CommunicationLost).target, LinkState::Disconnected);
    EXPECT_EQ(cell(LinkState::Connected, E::OutgoingInformation).actions, "send now");
    EXPECT_EQ(cell(LinkState::Disconnected, E::ConnectionRestored).target, LinkState::Resynchronizing);
    EXPECT_EQ(cell(LinkState::Disconnected, E::CommunicationLost).kind, "Ignore");
    EXPECT_EQ(cell(LinkState::Resynchronizing, E::CommunicationLost).target, LinkState::Disconnected);
    EXPECT_EQ(cell(LinkState::Resynchronizing, E::SynchronizationComplete).target, LinkState::Connected);
    for (LinkState s : {LinkState::Connecting, LinkState::Connected, LinkState::Disconnected, LinkState::Resynchronizing})
        EXPECT_EQ(cell(s, E::PowerOff).target, LinkState::Off);
    EXPECT_EQ(cell(LinkState::Off, E::OutgoingInformation).kind, "Illegal");
}

// ---- Connecting row -----------------------------------------------------------------------------
TEST_F(CommTest, SM3_Connecting_ConnectionEstablished_Connected) {
    EXPECT_EQ(st(), LinkState::Connecting);
    step();
    EXPECT_EQ(st(), LinkState::Connected);
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(log->all().back().type, EventType::COMM_STATE_CHANGED);
}

TEST_F(CommTest, SM3_Connecting_CommunicationLost_Disconnected_C4) {
    monitor.setOnline(false);
    step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod));
    EXPECT_EQ(st(), LinkState::Disconnected);
}

TEST_F(CommTest, SM3_Connecting_OutgoingInformation_Queued) {
    comm->publishStatus(Mode::Startup, false, "boot");
    EXPECT_EQ(comm->unsentMessageCount(), 1);
    EXPECT_TRUE(monitor.received().empty());
    step();
    EXPECT_EQ(receivedOfType("status"), 1);
}

TEST_F(CommTest, SM3_Connecting_PowerOff_Off) {
    comm->powerOff();
    EXPECT_EQ(st(), LinkState::Off);
}

TEST_F(CommTest, SM3_Connecting_ConnectionRestored_Ignore) {
    // connectionRestored is only fired from Disconnected; a first success in Connecting is
    // connectionEstablished. Checked through the table:
    EXPECT_EQ(CommunicationManager::transitionFor(LinkState::Connecting, LinkEvent::ConnectionRestored).kind, "Ignore");
}

// ---- Connected row ------------------------------------------------------------------------------
TEST_F(CommTest, SM3_Connected_ConnectionEstablished_Ignore) {
    toConnected();
    const auto before = changes.size();
    step(20);  // many more successful exchanges
    EXPECT_EQ(st(), LinkState::Connected);
    EXPECT_EQ(changes.size(), before);
}

TEST_F(CommTest, SM3_Connected_CommunicationLost_Disconnected) {
    toConnected();
    monitor.setOnline(false);
    step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod) + 1);
    EXPECT_EQ(st(), LinkState::Disconnected);
    EXPECT_EQ(changes.back(), std::make_pair(LinkState::Connected, LinkState::Disconnected));
}

TEST_F(CommTest, SM3_Connected_OutgoingInformation_SentNow) {
    toConnected();
    Fault f;
    f.type = FaultType::StorageNotClosed;
    f.drawer = "D3";
    f.message = "A drawer did not lock (drawer D3).";
    comm->publishAlarm(f);  // no tick
    EXPECT_EQ(receivedOfType("alarm"), 1);
    EXPECT_EQ(comm->unsentMessageCount(), 0);
}

TEST_F(CommTest, SM3_Connected_PowerOff_Off) {
    toConnected();
    comm->powerOff();
    EXPECT_EQ(st(), LinkState::Off);
}

// ---- Disconnected row ---------------------------------------------------------------------------
TEST_F(CommTest, SM3_Disconnected_ConnectionRestored_Resynchronizing) {
    toDisconnected();
    cfg.maxBatchSize = 1;
    restart();  // smaller batches so resynchronization takes several exchanges
    monitor.setOnline(false);
    step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod) + 1);
    ASSERT_EQ(st(), LinkState::Disconnected);
    for (int i = 0; i < 5; ++i) log->record(EventType::AUTH_FAILURE, "N100", "FAILED", "");
    monitor.setOnline(true);
    step(static_cast<int>(cfg.heartbeatPeriod / cfg.tickPeriod));
    EXPECT_EQ(st(), LinkState::Resynchronizing);
}

TEST_F(CommTest, SM3_Disconnected_OutgoingInformation_Queued) {
    toDisconnected();
    comm->publishMaintenance("entered maintenance mode", "T300");
    EXPECT_EQ(comm->unsentMessageCount(), 1);
    EXPECT_EQ(receivedOfType("maintenance"), 0);
}

TEST_F(CommTest, SM3_Disconnected_CommunicationLost_Ignore) {
    toDisconnected();
    const auto before = changes.size();
    step(50);
    EXPECT_EQ(st(), LinkState::Disconnected);
    EXPECT_EQ(changes.size(), before);
}

TEST_F(CommTest, SM3_Disconnected_PowerOff_Off) {
    toDisconnected();
    comm->powerOff();
    EXPECT_EQ(st(), LinkState::Off);
}

// ---- Resynchronizing row ------------------------------------------------------------------------
class CommResyncTest : public CommTest {
protected:
    void SetUp() override {
        cfg.maxBatchSize = 2;
        build();
        toDisconnected();
        for (int i = 0; i < 20; ++i) log->record(EventType::AUTH_FAILURE, "N100", "FAILED", std::to_string(i));
        monitor.setOnline(true);
        while (st() == LinkState::Disconnected) step();
        ASSERT_EQ(st(), LinkState::Resynchronizing);
    }
};

TEST_F(CommResyncTest, SM3_Resynchronizing_SynchronizationComplete_Connected) {
    step(30);
    EXPECT_EQ(st(), LinkState::Connected);
    EXPECT_EQ(comm->unsentEventCount() + comm->unsentMessageCount() <= 2, true);
}

TEST_F(CommResyncTest, SM3_Resynchronizing_CommunicationLost_Disconnected) {
    monitor.setOnline(false);
    step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod) + 1);
    EXPECT_EQ(st(), LinkState::Disconnected);
    EXPECT_GT(comm->unsentEventCount(), 0);  // retainStoredInformation
}

TEST_F(CommResyncTest, SM3_Resynchronizing_OutgoingInformation_Queued) {
    monitor.setOnline(false);
    comm->publishMaintenance("diagnostics", "T300");
    EXPECT_EQ(receivedOfType("maintenance"), 0);
    EXPECT_GE(comm->unsentMessageCount(), 1);
}

TEST_F(CommResyncTest, SM3_Resynchronizing_PowerOff_Off) {
    comm->powerOff();
    EXPECT_EQ(st(), LinkState::Off);
}

// ---- Off row ------------------------------------------------------------------------------------
TEST_F(CommTest, SM3_Off_OutgoingInformation_Illegal_Logged) {
    comm->powerOff();
    comm->publishStatus(Mode::Normal, false, "late");
    EXPECT_EQ(st(), LinkState::Off);
    EXPECT_EQ(log->all().back().type, EventType::UNEXPECTED_EVENT);
    EXPECT_EQ(log->all().back().outcome, "ILLEGAL");
}

TEST_F(CommTest, SM3_Off_PowerOff_Ignore) {
    comm->powerOff();
    const auto before = log->lastId();
    comm->powerOff();
    EXPECT_EQ(log->lastId(), before);
}

// ---- behaviour ----------------------------------------------------------------------------------
TEST_F(CommTest, NFR_PRF_003_LossDetectedWithinFiveSecondsStepping200ms) {
    toConnected();
    step(7);  // idle for a while: heartbeats only
    monitor.setOnline(false);
    const auto offlineAt = clock.now();
    while (st() == LinkState::Connected) {
        step();
        ASSERT_LT(clock.now() - offlineAt, Millis(5000));
    }
    EXPECT_EQ(st(), LinkState::Disconnected);
    EXPECT_LT(clock.now() - offlineAt, Millis(5000));
}

TEST_F(CommTest, BufferingGrowsNothingRemoved) {
    toDisconnected();
    const int before = comm->unsentEventCount();
    const auto logSize = log->all().size();
    for (int i = 0; i < 10; ++i) log->record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "");
    step(20);
    EXPECT_EQ(comm->unsentEventCount(), before + 10);
    EXPECT_EQ(log->all().size(), logSize + 10);  // INV-COM-2
}

TEST_F(CommTest, ResyncDeliversEveryEventOnceInOrder) {
    toDisconnected();
    for (int i = 0; i < 40; ++i) log->record(EventType::INVENTORY_CHANGED, "N100", "REMOVED", std::to_string(i));
    monitor.setOnline(true);
    step(50);
    ASSERT_EQ(st(), LinkState::Connected);
    std::vector<std::int64_t> ids;
    for (const auto& m : monitor.received()) {
        if (m.type == "event") ids.push_back(std::stoll(m.id.substr(4)));
    }
    ASSERT_EQ(static_cast<std::int64_t>(ids.size()), log->lastId());
    for (std::size_t i = 0; i < ids.size(); ++i) EXPECT_EQ(ids[i], static_cast<std::int64_t>(i + 1));
    EXPECT_EQ(monitor.duplicatesDropped(), 0);
    EXPECT_EQ(comm->unsentEventCount(), 0);
    EXPECT_EQ(comm->lastAckedEventId(), log->lastId());
}

TEST_F(CommTest, ResyncResendsFullStatusAndInventory) {
    toConnected();
    comm->publishStatus(Mode::Normal, false, "ready");
    comm->publishInventory(Medication{"M03", "Morphine", "D3", 10});
    const int statusBefore = receivedOfType("status");
    const int inventoryBefore = receivedOfType("inventory");
    monitor.setOnline(false);
    step(20);
    monitor.setOnline(true);
    step(20);
    EXPECT_EQ(receivedOfType("status"), statusBefore + 1);
    EXPECT_EQ(receivedOfType("inventory"), inventoryBefore + 1);
}

TEST_F(CommTest, LatestWinsStatusAndInventory) {
    toDisconnected();
    comm->publishStatus(Mode::Normal, false, "a");
    comm->publishStatus(Mode::Fault, true, "b");
    comm->publishInventory(Medication{"M03", "Morphine", "D3", 9});
    comm->publishInventory(Medication{"M03", "Morphine", "D3", 8});
    comm->publishInventory(Medication{"M01", "Acetaminophen", "D1", 20});
    EXPECT_EQ(comm->unsentMessageCount(), 3);
    monitor.setOnline(true);
    step(20);
    int status = 0;
    for (const auto& m : monitor.received()) {
        if (m.type == "status") {
            ++status;
            EXPECT_NE(m.json.find("\"mode\":\"FAULT\""), std::string::npos);
        }
        if (m.type == "inventory" && m.json.find("M03") != std::string::npos) {
            EXPECT_NE(m.json.find("\"quantity\":8"), std::string::npos);
        }
    }
    EXPECT_GE(status, 1);  // the stale NORMAL status was never sent (resync re-sends the latest one)
}

TEST_F(CommTest, AlarmsNeverDropped) {
    toDisconnected();
    Fault f;
    for (int i = 0; i < 5; ++i) {
        f.type = FaultType::SensorFailure;
        f.drawer = "D" + std::to_string(i);
        comm->publishAlarm(f);
    }
    EXPECT_EQ(comm->unsentMessageCount(), 5);
    monitor.setOnline(true);
    step(20);
    EXPECT_EQ(receivedOfType("alarm"), 5);
}

TEST_F(CommTest, OutboxAndCursorSurviveRestart) {
    toConnected();
    for (int i = 0; i < 3; ++i) log->record(EventType::AUTH_SUCCESS, "N100", "SUCCESS", "");
    step(3);
    const auto cursor = comm->lastAckedEventId();
    EXPECT_EQ(cursor, log->lastId());
    monitor.setOnline(false);
    comm->publishMaintenance("entered maintenance mode", "T300");
    comm->publishStatus(Mode::Maintenance, false, "m");
    restart();
    EXPECT_EQ(comm->lastAckedEventId(), cursor);
    EXPECT_EQ(comm->unsentMessageCount(), 2);
    monitor.setOnline(true);
    step(10);
    EXPECT_EQ(receivedOfType("maintenance"), 1);
    EXPECT_EQ(monitor.duplicatesDropped(), 0);  // nothing already confirmed was re-sent
}

TEST_F(CommTest, C8_ConfigUpdateAppliedAndLogged) {
    toConnected();
    monitor.queueConfig("heartbeatPeriodMs", "500");
    step();
    EXPECT_EQ(comm->heartbeatPeriod(), Millis(500));
    bool applied = false;
    for (const auto& r : log->all()) {
        if (r.type == EventType::CONFIG_UPDATED && r.outcome == "APPLIED") applied = true;
    }
    EXPECT_TRUE(applied);
}

TEST_F(CommTest, C8_ConfigUnknownKeyOrBadValueRejectedAndLogged) {
    toConnected();
    monitor.queueConfig("sessionTimeoutMs", "1");
    monitor.queueConfig("heartbeatPeriodMs", "999999");
    step();
    EXPECT_EQ(comm->heartbeatPeriod(), cfg.heartbeatPeriod);
    int rejected = 0;
    for (const auto& r : log->all()) rejected += (r.type == EventType::CONFIG_UPDATED && r.outcome == "REJECTED");
    EXPECT_EQ(rejected, 2);
}

TEST_F(CommTest, C7_NoFunctionRequiresMonitoringSystem) { EXPECT_FALSE(comm->requiresMonitoringSystem()); }

// FR-COM-004 / NFR-AVL-001: the cabinet keeps dispensing while the link is down.
TEST_F(CommTest, LocalDispensingWorksWhileOffline) {
    SimulatedCabinet cab(*storage, cfg);
    AccessControlManager access(*log, clock, cfg);
    InventoryManager inv(*storage, *log, access, cfg);
    FaultDetector faults(cab, *log, clock, cfg);
    OperationalModeManager modes(access, faults, *log, cab);
    DispensingController disp(access, inv, modes, faults, cab, *log, *storage, clock, cfg);
    faults.addListener([&](const Fault& f) { modes.onFaultDetected(f); });
    faults.addListener([&](const Fault& f) { disp.onFaultDetected(f); });
    comm->addLinkListener([&](LinkState, LinkState to) {
        if (to == LinkState::Disconnected) faults.reportCommunicationLoss();
    });
    modes.completeStartup({});
    monitor.setOnline(false);
    step(static_cast<int>(cfg.commLossTimeout / cfg.tickPeriod) + 1);
    ASSERT_EQ(comm->state(), LinkState::Disconnected);
    ASSERT_EQ(modes.mode(), Mode::Normal);
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    ASSERT_TRUE(disp.requestDispense("M03", 2).ok());
    cab.simOpen("D3");
    cab.simRemove("D3", 2);
    disp.tick();
    cab.simClose("D3");
    disp.tick();
    EXPECT_EQ(disp.state(), DispenseState::Completed);
    EXPECT_EQ(inv.quantityOf("M03"), 8);
}
