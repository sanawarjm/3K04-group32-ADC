#include "adc/access_control_manager.hpp"

#include <cstdint>

namespace adc {

namespace {

// ---- Secret 1: credential check -------------------------------------------------------------
// FNV-1a 64-bit hash of "<salt>:<pin>". Demo grade only (listed as a known limitation): a real
// cabinet would use a slow password hash and the hospital's identity service.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;  // FNV-1a 64 algorithm constant
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;              // FNV-1a 64 algorithm constant
constexpr int kBitsPerByte = 8;
constexpr int kHashBytes = 8;
constexpr std::uint64_t kByteMask = 0xFF;

std::uint64_t fnv1a64(const std::string& text) {
    std::uint64_t hash = kFnvOffsetBasis;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= kFnvPrime;
    }
    return hash;
}

// Compares every byte (no early exit) so the time taken does not depend on where they differ.
bool sameHash(std::uint64_t a, std::uint64_t b) {
    std::uint64_t diff = 0;
    for (int i = 0; i < kHashBytes; ++i) {
        diff |= ((a >> (i * kBitsPerByte)) & kByteMask) ^ ((b >> (i * kBitsPerByte)) & kByteMask);
    }
    return diff == 0;
}

// Fake demo users (README). Only salted hashes are stored; the PINs are in the README only.
struct DemoUser {
    const char* badge;
    const char* name;
    Role role;
    const char* salt;
    std::uint64_t pinHash;
};
const DemoUser kDemoUsers[] = {
    {"N100", "Nurse Alex Chen", Role::Nurse, "salt-7f3a", 0x4ed170ceaa060a51ULL},
    {"N101", "Nurse Jordan Lee", Role::Nurse, "salt-c21e", 0xcdc06b1f3ed7f1e3ULL},
    {"P200", "Pharmacist Priya Shah", Role::Pharmacist, "salt-90bd", 0xf7ab5ffb939c7253ULL},
    {"T300", "Technician Tom Reyes", Role::Technician, "salt-4e58", 0x6a5b0ef5b6f23948ULL},
};
const char* const kDummySalt = "salt-none";  // used for unknown badges so both paths do the same work

// ---- Secret 2: the role -> permission table (D1 sections 1.4 and 7, use case diagram) --------
// One row per permission; columns are Nurse, Pharmacist, Technician. Change a row here if the
// hospital changes its policy.
struct PermissionRow {
    Permission permission;
    bool nurse;
    bool pharmacist;
    bool technician;
};
const PermissionRow kPermissionTable[] = {
    //  permission                       Nurse  Pharm  Tech     D1 source
    {Permission::Dispense,               true,  true,  false},  // use case diagram; US-DISP-01
    {Permission::ViewInventory,          true,  true,  false},  // use case "View Inventory"
    {Permission::Replenish,              false, true,  false},  // "Replenish Inventory"; US-REL-01
    {Permission::ViewLog,                false, true,  true},   // NFR-SEC-002
    {Permission::EnterMaintenance,       false, false, true},   // FR-MODE-003/004
    {Permission::ExitMaintenance,        false, false, true},   // FR-MODE-003/004
    {Permission::RunDiagnostics,         false, false, true},   // "Run Diagnostic Check"
    {Permission::ClearFault,             false, false, true},   // "Acknowledge/Clear Fault"; NFR-SAF-002
    {Permission::ModifyAuditRecords,     false, false, false},  // NFR-AUD-005: nobody
};

const char* const kSignInFailed = "Sign-in failed: badge or PIN not recognised.";
const char* const kNobody = "-";

}  // namespace

AccessControlManager::AccessControlManager(EventLogger& logger, const Clock& clock, const AdcConfig& config)
    : logger_(logger), clock_(clock), sessionTimeout_(config.sessionTimeout) {
    for (const DemoUser& u : kDemoUsers) users_[u.badge] = UserRecord{u.name, u.role, u.salt, u.pinHash};
}

bool AccessControlManager::roleHasPermission(Role role, Permission p) {
    for (const PermissionRow& row : kPermissionTable) {
        if (row.permission != p) continue;
        switch (role) {
            case Role::Nurse: return row.nurse;
            case Role::Pharmacist: return row.pharmacist;
            case Role::Technician: return row.technician;
        }
    }
    return false;
}

OpResult AccessControlManager::authenticate(const Badge& badge, const std::string& pin) {
    logout("new sign-in");
    const auto found = users_.find(badge);
    // AUTH-LOGIN: R1 unknown badge, R2 wrong PIN, R3 valid. R1 and R2 do the same work.
    const std::string salt = (found != users_.end()) ? found->second.salt : kDummySalt;
    const std::uint64_t expected = (found != users_.end()) ? found->second.pinHash : 0;
    const bool match = sameHash(fnv1a64(salt + ":" + pin), expected);
    if (found == users_.end() || !match) {
        logger_.record(EventType::AUTH_FAILURE, badge, "FAILED", "badge or PIN not recognised");
        return OpResult::failure(ResultCode::AuthFailed, kSignInFailed);
    }
    current_ = badge;
    lastActivity_ = clock_.now();
    logger_.record(EventType::AUTH_SUCCESS, badge, "SUCCESS", std::string("role=") + toString(found->second.role));
    return OpResult::success("Signed in as " + found->second.name + " (" + toString(found->second.role) + ").");
}

void AccessControlManager::logout(const std::string& reason) {
    if (!current_) return;
    logger_.record(EventType::SESSION_ENDED, *current_, "ENDED", "reason=" + reason);
    current_.reset();
}

bool AccessControlManager::isAuthenticated() const { return current_.has_value(); }

Badge AccessControlManager::currentUser() const { return current_ ? *current_ : Badge(); }

std::string AccessControlManager::currentUserName() const {
    return current_ ? users_.at(*current_).name : std::string();
}

std::optional<Role> AccessControlManager::currentRole() const {
    if (!current_) return std::nullopt;
    return users_.at(*current_).role;
}

bool AccessControlManager::hasPermission(Permission p) const {
    const auto role = currentRole();
    return role.has_value() && roleHasPermission(*role, p);
}

OpResult AccessControlManager::authorize(Permission p, const std::string& actionText) {
    if (!current_) {
        logger_.record(EventType::ACCESS_DENIED, kNobody, "DENIED",
                       std::string("permission=") + toString(p) + " action=" + actionText + " (not signed in)");
        return OpResult::failure(ResultCode::NotAuthenticated, "Please sign in first to " + actionText + ".");
    }
    recordActivity();
    if (!hasPermission(p)) {
        logger_.record(EventType::ACCESS_DENIED, *current_, "DENIED",
                       std::string("permission=") + toString(p) + " action=" + actionText);
        return OpResult::failure(ResultCode::PermissionDenied, "You are not authorised to " + actionText + ".");
    }
    return OpResult::success("");
}

void AccessControlManager::recordActivity() {
    if (current_) lastActivity_ = clock_.now();
}

void AccessControlManager::tick(bool busy) {
    if (!current_) return;
    if (busy) {
        lastActivity_ = clock_.now();  // C2: held while the user is at the drawer
        return;
    }
    if (clock_.now() - lastActivity_ >= sessionTimeout_) logout("inactivity");
}

std::chrono::milliseconds AccessControlManager::sessionRemaining() const {
    if (!current_) return std::chrono::milliseconds(0);
    const auto left = sessionTimeout_ - (clock_.now() - lastActivity_);
    return left.count() > 0 ? left : std::chrono::milliseconds(0);
}

}  // namespace adc
