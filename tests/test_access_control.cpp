#include <gtest/gtest.h>

#include <chrono>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/event_logger.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class AccessTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    AdcConfig cfg = adc_test::testConfig(dir);
    ManualClock clock;
    Storage storage{dir.path()};
    EventLogger log{storage, clock};
    AccessControlManager access{log, clock, cfg};

    int count(EventType t) const {
        int n = 0;
        for (const auto& r : log.all()) n += (r.type == t) ? 1 : 0;
        return n;
    }
};

TEST_F(AccessTest, AuthLogin_R1_UnknownBadge_Fails) {
    const OpResult r = access.authenticate("X999", "1234");
    EXPECT_EQ(r.code, ResultCode::AuthFailed);
    EXPECT_FALSE(access.isAuthenticated());
    EXPECT_EQ(count(EventType::AUTH_FAILURE), 1);
    EXPECT_EQ(log.all().back().user, "X999");
}

TEST_F(AccessTest, AuthLogin_R2_WrongPin_Fails) {
    const OpResult r = access.authenticate("N100", "9999");
    EXPECT_EQ(r.code, ResultCode::AuthFailed);
    EXPECT_FALSE(access.isAuthenticated());
    EXPECT_EQ(log.all().back().type, EventType::AUTH_FAILURE);
    EXPECT_EQ(log.all().back().user, "N100");
}

TEST_F(AccessTest, AuthLogin_R1_R2_SameMessage) {
    const OpResult unknown = access.authenticate("X999", "1234");
    const OpResult wrong = access.authenticate("N100", "9999");
    EXPECT_EQ(unknown.message, wrong.message);
    EXPECT_EQ(wrong.message, "Sign-in failed: badge or PIN not recognised.");
}

TEST_F(AccessTest, AuthLogin_R3_Valid_SessionStartedRoleSet) {
    const OpResult r = access.authenticate("N100", "1234");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(access.isAuthenticated());
    EXPECT_EQ(access.currentUser(), "N100");
    EXPECT_EQ(access.currentUserName(), "Nurse Alex Chen");
    EXPECT_EQ(access.currentRole(), Role::Nurse);
    EXPECT_EQ(log.all().back().type, EventType::AUTH_SUCCESS);
}

TEST_F(AccessTest, AllDemoUsersSignIn) {
    EXPECT_TRUE(access.authenticate("N101", "4321").ok());
    EXPECT_EQ(access.currentRole(), Role::Nurse);
    EXPECT_TRUE(access.authenticate("P200", "2345").ok());
    EXPECT_EQ(access.currentRole(), Role::Pharmacist);
    EXPECT_TRUE(access.authenticate("T300", "3456").ok());
    EXPECT_EQ(access.currentRole(), Role::Technician);
}

TEST_F(AccessTest, AuthenticateEndsPreviousSession) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    ASSERT_TRUE(access.authenticate("P200", "2345").ok());
    EXPECT_EQ(access.currentUser(), "P200");
    EXPECT_EQ(count(EventType::SESSION_ENDED), 1);
    ASSERT_FALSE(access.authenticate("P200", "0000").ok());
    EXPECT_FALSE(access.isAuthenticated());  // a failed sign-in also ends the old session
}

TEST_F(AccessTest, PermissionTable_EveryCell) {
    struct Cell { Permission p; bool nurse, pharm, tech; };
    const Cell expected[] = {
        {Permission::Dispense, true, true, false},
        {Permission::ViewInventory, true, true, false},
        {Permission::Replenish, false, true, false},
        {Permission::ViewLog, false, true, true},
        {Permission::EnterMaintenance, false, false, true},
        {Permission::ExitMaintenance, false, false, true},
        {Permission::RunDiagnostics, false, false, true},
        {Permission::ClearFault, false, false, true},
        {Permission::ModifyAuditRecords, false, false, false},
    };
    for (const Cell& c : expected) {
        EXPECT_EQ(AccessControlManager::roleHasPermission(Role::Nurse, c.p), c.nurse) << toString(c.p);
        EXPECT_EQ(AccessControlManager::roleHasPermission(Role::Pharmacist, c.p), c.pharm) << toString(c.p);
        EXPECT_EQ(AccessControlManager::roleHasPermission(Role::Technician, c.p), c.tech) << toString(c.p);
    }
}

TEST_F(AccessTest, HasPermissionFollowsSignedInRole) {
    EXPECT_FALSE(access.hasPermission(Permission::Dispense));  // nobody signed in
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    EXPECT_TRUE(access.hasPermission(Permission::Dispense));
    EXPECT_FALSE(access.hasPermission(Permission::Replenish));
    const auto before = log.lastId();
    access.hasPermission(Permission::ViewLog);
    EXPECT_EQ(log.lastId(), before);  // pure query
}

TEST_F(AccessTest, FR_AUTH_003_DeniedActionLogsAccessDenied) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    const OpResult r = access.authorize(Permission::Replenish, "replenish medication");
    EXPECT_EQ(r.code, ResultCode::PermissionDenied);
    EXPECT_EQ(r.message, "You are not authorised to replenish medication.");
    const auto rec = log.all().back();
    EXPECT_EQ(rec.type, EventType::ACCESS_DENIED);
    EXPECT_EQ(rec.user, "N100");
    EXPECT_NE(rec.details.find("Replenish"), std::string::npos);
}

TEST_F(AccessTest, AuthorizeWithoutSignIn_NotAuthenticated) {
    const OpResult r = access.authorize(Permission::Dispense, "request medication");
    EXPECT_EQ(r.code, ResultCode::NotAuthenticated);
    EXPECT_EQ(log.all().back().type, EventType::ACCESS_DENIED);
}

TEST_F(AccessTest, NFR_AUD_005_NobodyMayModifyAuditRecords) {
    for (auto cred : {std::pair<const char*, const char*>{"N100", "1234"}, {"P200", "2345"}, {"T300", "3456"}}) {
        ASSERT_TRUE(access.authenticate(cred.first, cred.second).ok());
        EXPECT_EQ(access.authorize(Permission::ModifyAuditRecords, "edit audit records").code,
                  ResultCode::PermissionDenied);
    }
}

TEST_F(AccessTest, Logout) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    access.logout("user signed out");
    EXPECT_FALSE(access.isAuthenticated());
    EXPECT_EQ(access.currentUser(), "");
    EXPECT_FALSE(access.currentRole().has_value());
    EXPECT_EQ(log.all().back().type, EventType::SESSION_ENDED);
    const auto before = log.lastId();
    access.logout("again");
    EXPECT_EQ(log.lastId(), before);  // no session, nothing logged
}

TEST_F(AccessTest, NFR_SEC_003_TimeoutAtExactly60sNot59) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    clock.advance(Millis(59999));
    access.tick(false);
    EXPECT_TRUE(access.isAuthenticated());
    EXPECT_EQ(access.sessionRemaining(), Millis(1));
    clock.advance(Millis(1));
    access.tick(false);
    EXPECT_FALSE(access.isAuthenticated());
    const auto rec = log.all().back();
    EXPECT_EQ(rec.type, EventType::SESSION_ENDED);
    EXPECT_EQ(rec.details, "reason=inactivity");
}

TEST_F(AccessTest, NFR_SEC_003_ActivityRestartsTimer) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    clock.advance(Millis(50000));
    access.recordActivity();
    clock.advance(Millis(50000));
    access.tick(false);
    EXPECT_TRUE(access.isAuthenticated());
}

TEST_F(AccessTest, C2_TimerHeldWhileBusy) {
    ASSERT_TRUE(access.authenticate("N100", "1234").ok());
    for (int i = 0; i < 1000; ++i) {  // 200 s at the drawer
        clock.advance(cfg.tickPeriod);
        access.tick(true);
    }
    EXPECT_TRUE(access.isAuthenticated());
    clock.advance(cfg.sessionTimeout);
    access.tick(false);
    EXPECT_FALSE(access.isAuthenticated());
}

TEST_F(AccessTest, NFR_PRF_001_AuthenticationWellUnderFiveSeconds) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) access.authenticate("N100", i % 2 ? "1234" : "0000");
    const auto perCall = (std::chrono::steady_clock::now() - start) / 100;
    EXPECT_LT(perCall, std::chrono::milliseconds(100));
}

TEST_F(AccessTest, NFR_SEC_001_NoPinInAnyRecordOrMessage) {
    std::vector<std::string> messages;
    messages.push_back(access.authenticate("N100", "1234").message);
    messages.push_back(access.authenticate("N100", "9876").message);
    messages.push_back(access.authenticate("P200", "2345").message);
    messages.push_back(access.authenticate("T300", "3456").message);
    messages.push_back(access.authorize(Permission::Dispense, "request medication").message);
    for (const std::string pin : {"1234", "9876", "2345", "3456"}) {
        for (const auto& r : log.all()) {
            for (const std::string& f : {r.user, r.medication, r.outcome, r.details})
                EXPECT_EQ(f.find(pin), std::string::npos) << f;
        }
        for (const auto& m : messages) EXPECT_EQ(m.find(pin), std::string::npos) << m;
    }
}
