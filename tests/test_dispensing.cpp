#include <gtest/gtest.h>

#include <memory>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/dispensing_controller.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/simulated_cabinet.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

// The whole local control stack (everything except communication), wired like AdcSystem.
struct Stack {
    Storage storage;
    EventLogger log;
    SimulatedCabinet cab;
    AccessControlManager access;
    InventoryManager inv;
    FaultDetector faults;
    OperationalModeManager modes;
    DispensingController disp;

    Stack(const AdcConfig& cfg, ManualClock& clock)
        : storage(cfg.dataDir),
          log(storage, clock),
          cab(storage, cfg),
          access(log, clock, cfg),
          inv(storage, log, access, cfg),
          faults(cab, log, clock, cfg),
          modes(access, faults, log, cab),
          disp(access, inv, modes, faults, cab, log, storage, clock, cfg) {
        faults.addListener([this](const Fault& f) { modes.onFaultDetected(f); });
        faults.addListener([this](const Fault& f) { disp.onFaultDetected(f); });
    }
    void start() {
        disp.recoverInterruptedSession();
        for (const DrawerId& d : cab.drawers()) cab.o_setLock(d, LockCommand::Locked);
        modes.completeStartup(faults.selfTest(inv.expectedCounts()));
    }
};

class DispenseTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    std::unique_ptr<Stack> s = std::make_unique<Stack>(cfg, clock);

    void SetUp() override { s->start(); }

    void step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            clock.advance(cfg.tickPeriod);
            s->disp.tick();
            s->faults.check(s->disp.drawersUnderControl(), s->inv.expectedCounts());
            s->access.tick(s->disp.busy());
            s->modes.refreshIndicator();
        }
    }
    void nurse() { ASSERT_TRUE(s->access.authenticate("N100", "1234").ok()); }
    void pharmacist() { ASSERT_TRUE(s->access.authenticate("P200", "2345").ok()); }
    void technician() { ASSERT_TRUE(s->access.authenticate("T300", "3456").ok()); }
    DispenseState st() const { return s->disp.state(); }
    int outcomes(EventType t = EventType::DISPENSE_OUTCOME) const {
        int n = 0;
        for (const auto& r : s->log.all()) n += (r.type == t) ? 1 : 0;
        return n;
    }
    EventRecord lastOf(EventType t) const {
        EventRecord found;
        for (const auto& r : s->log.all()) {
            if (r.type == t) found = r;
        }
        return found;
    }
    void startDispenseM03(int qty = 2) {
        nurse();
        ASSERT_TRUE(s->disp.requestDispense("M03", qty).ok());
        ASSERT_EQ(st(), DispenseState::AwaitingRetrieval);
    }
    void toFaultMode() {
        s->faults.report(FaultType::SensorFailure, "D1", "test");
        ASSERT_EQ(s->modes.mode(), Mode::Fault);
    }
};

// ---- DSP-REQ ------------------------------------------------------------------------------------
TEST_F(DispenseTest, DspReq_R1_NotAuthenticated_Rejected) {
    const OpResult r = s->disp.requestDispense("M03", 2);
    EXPECT_EQ(r.code, ResultCode::NotAuthenticated);
    EXPECT_EQ(st(), DispenseState::Rejected);
    EXPECT_EQ(outcomes(), 1);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).outcome, "REJECTED");
    EXPECT_EQ(lastOf(EventType::ACCESS_DENIED).type, EventType::ACCESS_DENIED);
}

TEST_F(DispenseTest, DspReq_R1_LacksDispensePermission_Rejected) {
    technician();
    EXPECT_EQ(s->disp.requestDispense("M03", 2).code, ResultCode::PermissionDenied);
    EXPECT_EQ(st(), DispenseState::Rejected);
    EXPECT_EQ(outcomes(), 1);
    EXPECT_EQ(lastOf(EventType::ACCESS_DENIED).user, "T300");
}

TEST_F(DispenseTest, DspReq_R2_SessionActive_Rejected) {
    startDispenseM03();
    const OpResult r = s->disp.requestDispense("M01", 1);
    EXPECT_EQ(r.code, ResultCode::SessionActive);
    EXPECT_EQ(st(), DispenseState::AwaitingRetrieval);  // the running session is unchanged
    EXPECT_EQ(outcomes(), 1);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).medication, "M01");
    EXPECT_EQ(s->cab.snapshot().drawers[0].commanded, LockCommand::Locked);  // D1 never unlocked
}

TEST_F(DispenseTest, DspReq_R2_PreviousDrawerStillOpen_Rejected) {
    startDispenseM03(1);
    s->cab.simOpen("D3");
    step();
    s->disp.cancel();  // AwaitingRetrieval -> Cancelled while the drawer is open
    ASSERT_EQ(st(), DispenseState::Cancelled);
    EXPECT_EQ(s->disp.requestDispense("M01", 1).code, ResultCode::SessionActive);
    EXPECT_EQ(st(), DispenseState::Rejected);
}

TEST_F(DispenseTest, DspReq_R3_UnknownMedication_Rejected) {
    nurse();
    EXPECT_EQ(s->disp.requestDispense("M99", 1).code, ResultCode::UnknownMedication);
    EXPECT_EQ(st(), DispenseState::Rejected);
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, DspReq_R4_QuantityBelowOne_Rejected) {
    nurse();
    EXPECT_EQ(s->disp.requestDispense("M03", 0).code, ResultCode::InvalidQuantity);
    EXPECT_EQ(s->disp.requestDispense("M03", -3).code, ResultCode::InvalidQuantity);
    EXPECT_EQ(outcomes(), 2);
}

TEST_F(DispenseTest, DspReq_R5_FaultMode_Rejected) {
    nurse();
    toFaultMode();
    const OpResult r = s->disp.requestDispense("M03", 1);
    EXPECT_EQ(r.code, ResultCode::ModeNotAllowed);
    EXPECT_EQ(r.message, "Dispensing is not available in FAULT mode.");
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, DspReq_R5_MaintenanceMode_Rejected) {
    technician();
    ASSERT_TRUE(s->modes.requestEnterMaintenance().ok());
    nurse();
    const OpResult r = s->disp.requestDispense("M03", 1);
    EXPECT_EQ(r.message, "Dispensing is not available in MAINTENANCE mode.");
}

TEST_F(DispenseTest, DspReq_R6_InsufficientStock_Rejected) {
    nurse();
    const OpResult r = s->disp.requestDispense("M03", 999);
    EXPECT_EQ(r.code, ResultCode::InsufficientStock);
    EXPECT_EQ(st(), DispenseState::Rejected);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).details.find("requested=999"), 0u);
}

TEST_F(DispenseTest, DspReq_R7_DrawerNotReady_Rejected) {
    nurse();
    s->cab.o_setLock("D3", LockCommand::Unlocked);  // lock disengaged before the request
    const OpResult r = s->disp.requestDispense("M03", 1);
    EXPECT_EQ(r.code, ResultCode::DrawerNotReady);
    EXPECT_EQ(r.message, "Drawer D3 is not ready. Call a technician.");
}

TEST_F(DispenseTest, DspReq_R8_Valid_AwaitingRetrievalOnlyThatDrawerUnlocked) {
    startDispenseM03();
    const auto snap = s->cab.snapshot();
    for (const auto& d : snap.drawers)
        EXPECT_EQ(d.commanded, d.id == "D3" ? LockCommand::Unlocked : LockCommand::Locked) << d.id;  // INV-DSP-1
    EXPECT_EQ(s->disp.drawersUnderControl(), (std::set<DrawerId>{"D3"}));
    EXPECT_EQ(s->disp.session().timeLeft, cfg.retrievalTimeout);
    EXPECT_EQ(outcomes(), 0);  // the outcome is written when the session ends
}

// ---- SM2 table: every cell matches D1 (covers transient AuthorizationPending cells) ------------
TEST_F(DispenseTest, SM2_Table_MatchesD1) {
    using S = DispenseState;
    using E = DispenseEvent;
    auto cell = [](S st, E ev) { return DispensingController::transitionFor(st, ev); };
    EXPECT_EQ(cell(S::Idle, E::DispenseRequest).target, S::AuthorizationPending);
    EXPECT_EQ(cell(S::Idle, E::MedicationRemoved).kind, "Reject");
    EXPECT_EQ(cell(S::AuthorizationPending, E::DispenseRequest).kind, "Stay");
    EXPECT_EQ(cell(S::AuthorizationPending, E::AuthorizationApproved).kind, "Guarded");
    EXPECT_EQ(cell(S::AuthorizationPending, E::AuthorizationApproved).target, S::AwaitingRetrieval);
    EXPECT_EQ(cell(S::AuthorizationPending, E::AuthorizationDenied).target, S::Rejected);
    EXPECT_EQ(cell(S::AuthorizationPending, E::CancelRequest).target, S::Cancelled);
    EXPECT_EQ(cell(S::AuthorizationPending, E::FaultDetected).target, S::Interrupted);
    EXPECT_EQ(cell(S::AwaitingRetrieval, E::MedicationRemoved).target, S::AwaitingClosure);
    EXPECT_EQ(cell(S::AwaitingRetrieval, E::CancelRequest).target, S::Cancelled);
    EXPECT_EQ(cell(S::AwaitingRetrieval, E::RetrievalTimeout).target, S::TimedOut);
    EXPECT_EQ(cell(S::AwaitingRetrieval, E::CompartmentClosed).kind, "Ignore");
    EXPECT_EQ(cell(S::AwaitingRetrieval, E::FaultDetected).target, S::Interrupted);
    EXPECT_EQ(cell(S::AwaitingClosure, E::MedicationRemoved).kind, "Stay");
    EXPECT_EQ(cell(S::AwaitingClosure, E::CancelRequest).kind, "Reject");
    EXPECT_EQ(cell(S::AwaitingClosure, E::CompartmentClosed).target, S::Completed);
    EXPECT_EQ(cell(S::AwaitingClosure, E::ClosureTimeout).target, S::TimedOut);
    EXPECT_EQ(cell(S::AwaitingClosure, E::FaultDetected).target, S::Interrupted);
    for (S t : {S::Rejected, S::Cancelled, S::TimedOut, S::Completed, S::Interrupted}) {
        EXPECT_EQ(cell(t, E::DispenseRequest).target, S::AuthorizationPending);
        EXPECT_EQ(cell(t, E::MedicationRemoved).kind, "Reject");
        for (E e : {E::AuthorizationApproved, E::AuthorizationDenied, E::CancelRequest, E::RetrievalTimeout,
                    E::CompartmentClosed, E::ClosureTimeout, E::FaultDetected})
            EXPECT_EQ(cell(t, e).kind, "Ignore");  // INV-DSP-3
    }
    // No cell is left undecided and none is Illegal.
    for (int a = 0; a <= static_cast<int>(S::Interrupted); ++a)
        for (int b = 0; b <= static_cast<int>(E::FaultDetected); ++b)
            EXPECT_NE(cell(static_cast<S>(a), static_cast<E>(b)).kind, "");
}

// ---- SM2 behaviour, row by row ------------------------------------------------------------------
TEST_F(DispenseTest, SM2_Idle_DispenseRequest_AuthorizationPending_BeginsSession) {
    nurse();
    s->disp.requestDispense("M03", 2);
    EXPECT_EQ(s->disp.session().user, "N100");
    EXPECT_EQ(s->disp.session().medication, "M03");
    EXPECT_EQ(s->disp.session().requested, 2);
}

TEST_F(DispenseTest, SM2_Idle_MedicationRemoved_LogUnexpectedEvent) {
    step();
    s->cab.simForceOpen("D1");
    s->cab.simRemove("D1", 1);
    step();
    EXPECT_EQ(st(), DispenseState::Idle);
    bool logged = false;
    for (const auto& r : s->log.all()) {
        if (r.type == EventType::UNEXPECTED_EVENT && r.details.find("medicationRemoved") != std::string::npos) logged = true;
    }
    EXPECT_TRUE(logged);
    EXPECT_EQ(s->inv.quantityOf("M01"), 20);  // records unchanged; the discrepancy is a fault
}

TEST_F(DispenseTest, SM2_Idle_CancelRequest_Ignore) {
    nurse();
    const auto before = s->log.lastId();
    EXPECT_EQ(s->disp.cancel().message, "There is no request to cancel.");
    EXPECT_EQ(s->log.lastId(), before);
}

TEST_F(DispenseTest, SM2_AuthorizationPending_AuthorizationApproved_AwaitingRetrieval) {
    startDispenseM03();
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Unlocked);
}

TEST_F(DispenseTest, SM2_AuthorizationPending_AuthorizationDenied_Rejected) {
    nurse();
    s->disp.requestDispense("M99", 1);
    EXPECT_EQ(st(), DispenseState::Rejected);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_DispenseRequest_StayRejectRequest) {
    startDispenseM03();
    s->disp.requestDispense("M03", 1);
    EXPECT_EQ(st(), DispenseState::AwaitingRetrieval);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_MedicationRemoved_AwaitingClosure) {
    startDispenseM03();
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    EXPECT_EQ(st(), DispenseState::AwaitingClosure);
    EXPECT_EQ(s->disp.session().timeLeft, cfg.closureTimeout);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_CancelRequest_Cancelled) {
    startDispenseM03();
    EXPECT_TRUE(s->disp.cancel().ok());
    EXPECT_EQ(st(), DispenseState::Cancelled);
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Locked);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).outcome, "CANCELLED");
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_RetrievalTimeout_TimedOut) {
    startDispenseM03();
    step(static_cast<int>(cfg.retrievalTimeout / cfg.tickPeriod));
    EXPECT_EQ(st(), DispenseState::TimedOut);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).outcome, "TIMED_OUT");
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Locked);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_CompartmentClosed_Ignore) {
    startDispenseM03();
    s->cab.simOpen("D3");
    step();
    s->cab.simClose("D3");
    step();
    EXPECT_EQ(st(), DispenseState::AwaitingRetrieval);
}

TEST_F(DispenseTest, SM2_AwaitingRetrieval_FaultDetected_Interrupted) {
    startDispenseM03();
    s->faults.report(FaultType::SensorFailure, "D1", "x");
    EXPECT_EQ(st(), DispenseState::Interrupted);
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Locked);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).outcome, "INTERRUPTED");
}

TEST_F(DispenseTest, SM2_AwaitingClosure_DispenseRequest_StayRejectRequest) {
    startDispenseM03();
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    EXPECT_EQ(s->disp.requestDispense("M01", 1).code, ResultCode::SessionActive);
    EXPECT_EQ(st(), DispenseState::AwaitingClosure);
}

TEST_F(DispenseTest, SM2_AwaitingClosure_MedicationRemoved_StayUpdateInventory) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    s->cab.simRemove("D3", 1);
    step();
    EXPECT_EQ(st(), DispenseState::AwaitingClosure);
    EXPECT_EQ(s->inv.quantityOf("M03"), 8);
    EXPECT_EQ(s->disp.session().moved, 2);
}

TEST_F(DispenseTest, SM2_AwaitingClosure_CancelRequest_RejectCloseTheDrawer) {
    startDispenseM03();
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 2);
    step();
    const OpResult r = s->disp.cancel();
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.message.find("Close the drawer"), std::string::npos);
    EXPECT_EQ(st(), DispenseState::AwaitingClosure);
    EXPECT_EQ(s->log.all().back().type, EventType::UNEXPECTED_EVENT);
}

TEST_F(DispenseTest, SM2_AwaitingClosure_CompartmentClosed_Completed) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 2);
    step();
    s->cab.simClose("D3");
    step();
    EXPECT_EQ(st(), DispenseState::Completed);
    EXPECT_EQ(s->cab.snapshot().drawers[2].lockEngaged, true);
    const auto rec = lastOf(EventType::DISPENSE_OUTCOME);
    EXPECT_EQ(rec.outcome, "COMPLETED");
    EXPECT_EQ(rec.user, "N100");
    EXPECT_EQ(rec.medication, "M03");
    EXPECT_EQ(rec.quantity, 2);
    EXPECT_TRUE(s->disp.drawersUnderControl().empty());
}

TEST_F(DispenseTest, SM2_AwaitingClosure_CompartmentClosed_WrongQuantity_IncompleteDispenseInterrupted) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    s->cab.simClose("D3");
    step();
    EXPECT_EQ(st(), DispenseState::Interrupted);
    EXPECT_TRUE(s->faults.isActive(FaultType::IncompleteDispense, "D3"));
    EXPECT_EQ(s->modes.mode(), Mode::Fault);
    EXPECT_EQ(s->inv.quantityOf("M03"), 9);
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, SM2_AwaitingClosure_ClosureTimeout_TimedOut_C5) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 2);
    step();
    step(static_cast<int>(cfg.closureTimeout / cfg.tickPeriod));
    EXPECT_EQ(st(), DispenseState::TimedOut);
    EXPECT_TRUE(s->faults.isActive(FaultType::IncompleteDispense, "D3"));
    EXPECT_EQ(s->modes.mode(), Mode::Fault);
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Locked);  // INV-DSP-1
    s->cab.simClose("D3");
    step();
    EXPECT_TRUE(s->cab.snapshot().drawers[2].lockEngaged);  // engages once closed
}

TEST_F(DispenseTest, SM2_AwaitingClosure_FaultDetected_Interrupted) {
    startDispenseM03();
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    s->faults.report(FaultType::StorageFailure, "", "x");
    EXPECT_EQ(st(), DispenseState::Interrupted);
}

TEST_F(DispenseTest, SM2_Completed_DispenseRequest_NewSession) {
    startDispenseM03(1);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    s->cab.simClose("D3");
    step();
    ASSERT_EQ(st(), DispenseState::Completed);
    EXPECT_TRUE(s->disp.requestDispense("M01", 1).ok());
    EXPECT_EQ(st(), DispenseState::AwaitingRetrieval);
    EXPECT_EQ(s->disp.session().medication, "M01");
}

TEST_F(DispenseTest, SM2_Terminal_MedicationRemoved_LogUnexpectedEvent) {
    startDispenseM03();
    s->disp.cancel();
    step();  // drawer closed -> released; baseline for untracked drawers
    s->cab.simForceOpen("D2");
    s->cab.simRemove("D2", 1);
    step();
    EXPECT_EQ(st(), DispenseState::Cancelled);  // INV-DSP-3
    bool logged = false;
    for (const auto& r : s->log.all()) {
        if (r.type == EventType::UNEXPECTED_EVENT && r.details.find("state=CANCELLED") != std::string::npos) logged = true;
    }
    EXPECT_TRUE(logged);
}

TEST_F(DispenseTest, SM2_Terminal_CancelRequest_Ignore) {
    startDispenseM03();
    s->disp.cancel();
    EXPECT_EQ(s->disp.cancel().message, "There is no request to cancel.");
    EXPECT_EQ(st(), DispenseState::Cancelled);
}

// ---- end-to-end behaviours ----------------------------------------------------------------------
TEST_F(DispenseTest, HappyPath_InventoryUpdatedBeforeClosingWithinOneTick) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 2);
    step(1);  // NFR-PRF-002: one tick (200 ms) later
    EXPECT_EQ(s->inv.quantityOf("M03"), 8);
    EXPECT_EQ(lastOf(EventType::INVENTORY_CHANGED).details.find("10->8"), 0u);
    s->cab.simClose("D3");
    step();
    EXPECT_EQ(st(), DispenseState::Completed);
    EXPECT_EQ(s->modes.mode(), Mode::Normal);
    for (int i = 0; i < 30; ++i) step();
    EXPECT_TRUE(s->faults.activeFaults().empty());
}

TEST_F(DispenseTest, C5_RetrievalTimeout_IncompleteDispense_FaultMode) {
    startDispenseM03();
    s->cab.simOpen("D3");
    step(static_cast<int>(cfg.retrievalTimeout / cfg.tickPeriod));
    EXPECT_EQ(st(), DispenseState::TimedOut);
    EXPECT_TRUE(s->faults.isActive(FaultType::IncompleteDispense, "D3"));
    EXPECT_EQ(s->modes.mode(), Mode::Fault);
    EXPECT_FALSE(s->faults.isActive(FaultType::UnauthorizedAccess, "D3"));  // drawer still under control
}

TEST_F(DispenseTest, NothingRemoved_OpenAndCloseThenTimeout) {
    startDispenseM03();
    s->cab.simOpen("D3");
    step();
    s->cab.simClose("D3");
    step(static_cast<int>(cfg.retrievalTimeout / cfg.tickPeriod));
    EXPECT_EQ(st(), DispenseState::TimedOut);
    EXPECT_EQ(s->modes.mode(), Mode::Fault);
}

TEST_F(DispenseTest, FaultMidSession_InterruptedLockCommandedLaterRemovalsRecorded) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    step();
    s->cab.simFailSensor("D1", true);  // a different drawer fails
    step();
    ASSERT_EQ(st(), DispenseState::Interrupted);
    EXPECT_EQ(s->cab.snapshot().drawers[2].commanded, LockCommand::Locked);
    s->cab.simRemove("D3", 1);  // still open: a later removal
    step();
    EXPECT_EQ(s->inv.quantityOf("M03"), 9);
    s->cab.simClose("D3");
    step();
    EXPECT_TRUE(s->cab.snapshot().drawers[2].lockEngaged);
    EXPECT_TRUE(s->disp.drawersUnderControl().empty());
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, NFR_SAF_003_SecondDispenseFromSameSessionImpossible) {
    startDispenseM03(1);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    s->cab.simClose("D3");
    step();
    ASSERT_EQ(st(), DispenseState::Completed);
    EXPECT_FALSE(s->cab.simOpen("D3").ok());  // relocked: the same request cannot give more
    s->disp.cancel();
    EXPECT_EQ(st(), DispenseState::Completed);
}

TEST_F(DispenseTest, NFR_SAF_003_RestartMidSession_InterruptedNoResume) {
    startDispenseM03(2);
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    step();
    // Power cut: rebuild everything from storage.
    s.reset();
    s = std::make_unique<Stack>(cfg, clock);
    s->start();
    EXPECT_EQ(st(), DispenseState::Interrupted);
    EXPECT_EQ(outcomes(), 1);
    EXPECT_EQ(lastOf(EventType::DISPENSE_OUTCOME).outcome, "INTERRUPTED");
    EXPECT_EQ(s->inv.quantityOf("M03"), 9);
    EXPECT_EQ(s->modes.mode(), Mode::Fault);  // drawer D3 still open: start-up check fails
    s->cab.simClose("D3");
    step();
    // A second restart does not produce another outcome.
    s.reset();
    s = std::make_unique<Stack>(cfg, clock);
    s->start();
    EXPECT_EQ(st(), DispenseState::Idle);
    EXPECT_EQ(outcomes(), 1);
}

TEST_F(DispenseTest, INV_DSP_2_EveryAttemptOneOutcome) {
    nurse();
    s->disp.requestDispense("M99", 1);   // rejected
    s->disp.requestDispense("M03", 0);   // rejected
    s->disp.requestDispense("M03", 1);   // accepted
    s->disp.requestDispense("M01", 1);   // rejected while active
    s->disp.cancel();                    // cancelled
    EXPECT_EQ(outcomes(), 4);
}

TEST_F(DispenseTest, NFR_AVL_004_CommunicationLossDoesNotInterrupt) {
    startDispenseM03();
    s->faults.reportCommunicationLoss();
    EXPECT_EQ(st(), DispenseState::AwaitingRetrieval);
}

TEST_F(DispenseTest, Replenish_CountsAddedUnits) {
    pharmacist();
    ASSERT_TRUE(s->disp.requestReplenish("M04").ok());
    s->cab.simOpen("D4");
    s->cab.simAdd("D4", 8);
    step();
    EXPECT_EQ(s->inv.quantityOf("M04"), 20);
    s->cab.simClose("D4");
    step();
    EXPECT_EQ(st(), DispenseState::Completed);
    const auto rec = lastOf(EventType::REPLENISH_OUTCOME);
    EXPECT_EQ(rec.outcome, "COMPLETED");
    EXPECT_EQ(rec.quantity, 8);
    EXPECT_EQ(rec.user, "P200");
    EXPECT_TRUE(s->cab.snapshot().drawers[3].lockEngaged);
}

TEST_F(DispenseTest, RepReq_R1_NurseDenied) {
    nurse();
    EXPECT_EQ(s->disp.requestReplenish("M04").code, ResultCode::PermissionDenied);
    EXPECT_EQ(outcomes(EventType::REPLENISH_OUTCOME), 1);
}

TEST_F(DispenseTest, RepReq_R5_FaultModeRefused_MaintenanceAllowed_C1) {
    technician();
    ASSERT_TRUE(s->modes.requestEnterMaintenance().ok());
    pharmacist();
    EXPECT_TRUE(s->disp.requestReplenish("M04").ok());
    s->disp.cancel();
    step();  // the closed drawer is released on the next tick
    toFaultMode();
    EXPECT_EQ(s->disp.requestReplenish("M04").code, ResultCode::ModeNotAllowed);
}

TEST_F(DispenseTest, InventoryStorageFailure_InventoryUpdateFailedFault) {
    startDispenseM03();
    s->cab.simOpen("D3");
    s->cab.simRemove("D3", 1);
    s->storage.simulateWriteFailure(true);
    step();
    EXPECT_TRUE(s->faults.isActive(FaultType::InventoryUpdateFailed, "D3"));
    EXPECT_EQ(st(), DispenseState::Interrupted);
    s->storage.simulateWriteFailure(false);
    step();  // the change is retried
    EXPECT_EQ(s->inv.quantityOf("M03"), 9);
}
