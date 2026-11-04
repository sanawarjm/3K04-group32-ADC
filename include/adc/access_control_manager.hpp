#pragma once
// D1 component: Access Control Manager
// Responsibility:
//   Authenticates users, determines their role and permissions, denies protected actions
//   without permission, and ends inactive sessions.
// D1 user stories:
//   US-AUTH-01, US-SEC-01, US-PRF-01 (AC-01, AC-03), US-USB-01
// D1 requirements traced to this component (D1 section 6):
//   FR-AUTH-001, FR-AUTH-002, FR-AUTH-003, FR-AUTH-004, FR-INV-003, FR-DISP-001, FR-MODE-004,
//   NFR-SAF-003, NFR-SEC-001, NFR-SEC-002, NFR-SEC-003, NFR-PRF-001, NFR-AUD-004, NFR-AUD-005,
//   NFR-USB-001, NFR-USB-002, NFR-USB-004
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/access_control_manager.cpp.
//
// ---- Module Interface Specification (MG 1.2.1) --------------------------------
// Responsibility: decides who is signed in, what their role allows, and when the session ends.
// Secret:         how credentials are checked (salted FNV-1a 64 hashes, demo grade), the
//                 role -> permission table (one table in the .cpp), and the session rules.
// Uses:           EventLogger, Clock, AdcConfig, types.
// State invariant(s):
//   INV-ACC-1: isAuthenticated() <=> currentUser() != "" <=> currentRole() has a value.
//   INV-ACC-2: no state, return value, log record or message contains a PIN, salt or hash
//              (NFR-SEC-001).
//   INV-ACC-3: a session never lasts more than sessionTimeout past the last activity unless
//              the cabinet was busy with it (NFR-SEC-003, clarification C2).

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/event_logger.hpp"
#include "adc/types.hpp"

namespace adc {

class AccessControlManager {
public:
    // AccessControlManager(logger, clock, config)
    // Pre:         logger and clock outlive this object.
    // Post:        nobody is signed in; the four demo users of the README are known.
    // Effects:     none.
    AccessControlManager(EventLogger& logger, const Clock& clock, const AdcConfig& config);

    // authenticate(badge, pin) -> OpResult
    // Reqs:        FR-AUTH-001, FR-AUTH-002, FR-AUTH-004, NFR-SEC-001, NFR-PRF-001, NFR-USB-002
    // Pre:         none.
    // Post:        any previous session has ended. Ok: currentUser() == badge, currentRole() set,
    //              session timer restarted. AuthFailed: nobody is signed in.
    // Semantics:   function table AUTH-LOGIN in docs/specification.md (rows R1..R3).
    // Effects:     SESSION_ENDED (if a session was open), then AUTH_SUCCESS or AUTH_FAILURE,
    //              each with the badge only.
    // Exceptions:  none. R1 and R2 return the same message, so a badge cannot be probed.
    OpResult authenticate(const Badge& badge, const std::string& pin);

    // logout(reason)
    // Reqs:        FR-AUTH-001, NFR-SEC-003
    // Post:        !isAuthenticated().
    // Effects:     SESSION_ENDED "reason=<reason>" if a session was open; nothing otherwise.
    void logout(const std::string& reason);

    // isAuthenticated() -> true while a user is signed in (INV-ACC-1).
    bool isAuthenticated() const;

    // currentUser() -> badge of the signed-in user, "" if none.
    Badge currentUser() const;

    // currentUserName() -> display name, e.g. "Nurse Alex Chen", "" if none.
    std::string currentUserName() const;

    // currentRole() -> role of the signed-in user, empty if none.
    std::optional<Role> currentRole() const;

    // hasPermission(p) -> bool
    // Reqs:        FR-AUTH-002, NFR-SEC-002
    // Post:        true iff signed in and roleHasPermission(currentRole(), p). Pure query: no log.
    bool hasPermission(Permission p) const;

    // authorize(p, actionText) -> OpResult
    // Reqs:        FR-AUTH-003, FR-MODE-004, NFR-SEC-002, NFR-AUD-004, NFR-USB-001
    // Pre:         actionText is a short verb phrase, e.g. "replenish medication".
    // Post:        Ok iff hasPermission(p). Otherwise NotAuthenticated ("Please sign in ...")
    //              or PermissionDenied ("You are not authorised to <actionText>.").
    // Effects:     ACCESS_DENIED record (permission and action) on every denial; counts as
    //              activity when signed in.
    OpResult authorize(Permission p, const std::string& actionText);

    // recordActivity()
    // Reqs:        NFR-SEC-003
    // Post:        the inactivity timer restarts if signed in.
    void recordActivity();

    // tick(busy)
    // Reqs:        NFR-SEC-003 (clarification C2)
    // Pre:         called every tickPeriod.
    // Post:        if busy, the timer is held (restarted). Otherwise, if signed in and
    //              now - lastActivity >= sessionTimeout, the session has ended.
    // Effects:     SESSION_ENDED "reason=inactivity" when the session ends.
    void tick(bool busy);

    // sessionRemaining() -> time left before the inactivity timeout (0 if not signed in).
    std::chrono::milliseconds sessionRemaining() const;

    // roleHasPermission(role, p) -> bool
    // Reqs:        NFR-SEC-002, NFR-AUD-005
    // Post:        the cell of the role -> permission table (pure; used by tests and docs).
    static bool roleHasPermission(Role role, Permission p);

private:
    struct UserRecord {
        std::string name;
        Role role;
        std::string salt;
        std::uint64_t pinHash;
    };

    EventLogger& logger_;
    const Clock& clock_;
    std::chrono::milliseconds sessionTimeout_;
    std::map<Badge, UserRecord> users_;
    std::optional<Badge> current_;
    std::chrono::milliseconds lastActivity_{0};
};

}  // namespace adc
