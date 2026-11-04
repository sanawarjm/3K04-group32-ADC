#include "adc/fault_detector.hpp"

#include <utility>

namespace adc {

namespace {
const char* const kSystem = "ADC";
const char* const kCallTechnician = " Dispensing is stopped. Call a technician.";

std::string where(const DrawerId& d) { return d.empty() ? std::string() : " (drawer " + d + ")"; }
}  // namespace

FaultDetector::FaultDetector(HardwareInterface& hardware, EventLogger& logger, const Clock& clock,
                             const AdcConfig& config)
    : hardware_(hardware), logger_(logger), clock_(clock), lockConfirmTimeout_(config.lockConfirmTimeout) {}

bool FaultDetector::isCritical(FaultType type) { return type != FaultType::CommunicationLoss; }

std::string FaultDetector::messageFor(FaultType type, const DrawerId& d) {
    switch (type) {
        case FaultType::StorageNotClosed: return "A drawer did not lock" + where(d) + "." + kCallTechnician;
        case FaultType::SensorFailure: return "A drawer sensor is not responding" + where(d) + "." + kCallTechnician;
        case FaultType::IncompleteDispense:
            return "A dispense did not complete as expected" + where(d) + "." + kCallTechnician;
        case FaultType::InventoryUpdateFailed: return std::string("The inventory record could not be updated.") + kCallTechnician;
        case FaultType::StorageFailure: return std::string("Records cannot be saved.") + kCallTechnician;
        case FaultType::StartupCheckFailed: return "The start-up hardware check failed" + where(d) + "." + kCallTechnician;
        case FaultType::UnauthorizedAccess: return "A drawer was opened without authorization" + where(d) + "." + kCallTechnician;
        case FaultType::CommunicationLoss: return "Monitoring link down: cabinet working locally.";
    }
    return std::string("Unknown fault.") + kCallTechnician;
}

bool FaultDetector::raise(FaultType type, const DrawerId& drawer, const std::string& detail, bool notify) {
    if (isActive(type, drawer)) return false;  // INV-FLT-1
    Fault f;
    f.type = type;
    f.drawer = drawer;
    f.detail = detail;
    f.message = messageFor(type, drawer);
    f.critical = isCritical(type);
    active_.push_back(f);
    logger_.record(EventType::FAULT_DETECTED, kSystem, toString(type),
                   (drawer.empty() ? std::string() : "drawer=" + drawer + " ") + detail);  // INV-FLT-2
    if (notify) {
        for (const Listener& listener : listeners_) listener(f);
    }
    return true;
}

bool FaultDetector::report(FaultType type, const DrawerId& drawer, const std::string& detail) {
    return raise(type, drawer, detail, true);
}

void FaultDetector::check(const std::set<DrawerId>& authorizedDrawers, const std::map<DrawerId, int>& expectedCounts) {
    const auto now = clock_.now();
    for (const DrawerId& d : hardware_.drawers()) {
        const auto open = hardware_.i_drawerOpen(d);
        const auto locked = hardware_.i_lockEngaged(d);
        const auto count = hardware_.i_itemCount(d);
        const bool authorized = authorizedDrawers.count(d) > 0;
        // FLT-CHECK R1
        if (!open || !locked || !count) {
            unlockedSince_.erase(d);
            report(FaultType::SensorFailure, d, "no reading from a drawer sensor");
            continue;
        }
        // R2
        if (*open && !authorized) {
            unlockedSince_.erase(d);
            report(FaultType::UnauthorizedAccess, d, "drawer open without an authorized session");
            continue;
        }
        // R3 (timer part)
        if (!*open && !*locked && !authorized) {
            if (!unlockedSince_.count(d)) unlockedSince_[d] = now;
            if (now - unlockedSince_[d] >= lockConfirmTimeout_)
                report(FaultType::StorageNotClosed, d, "lock not engaged after the drawer closed");
            continue;
        }
        unlockedSince_.erase(d);
        // R4
        const auto expected = expectedCounts.find(d);
        if (!*open && *locked && !authorized && expected != expectedCounts.end() && *count != expected->second) {
            report(FaultType::IncompleteDispense, d,
                   "count discrepancy sensed=" + std::to_string(*count) + " recorded=" + std::to_string(expected->second));
        }
        // R5: no fault
    }
    // R6
    if (!logger_.healthy()) report(FaultType::StorageFailure, "", "audit records not saved");
}

bool FaultDetector::drawerHealthy(const DrawerId& d, const std::map<DrawerId, int>& expectedCounts,
                                  std::string* why) const {
    const auto open = hardware_.i_drawerOpen(d);
    const auto locked = hardware_.i_lockEngaged(d);
    const auto count = hardware_.i_itemCount(d);
    std::string problem;
    if (!open || !locked || !count) problem = "a sensor gives no reading";
    else if (*open) problem = "the drawer is open";
    else if (!*locked) problem = "the lock is not engaged";
    else {
        const auto expected = expectedCounts.find(d);
        if (expected != expectedCounts.end() && *count != expected->second)
            problem = "it holds " + std::to_string(*count) + " units but the record says " +
                      std::to_string(expected->second);
    }
    if (why) *why = problem;
    return problem.empty();
}

std::vector<std::string> FaultDetector::diagnose(const std::map<DrawerId, int>& expectedCounts) const {
    std::vector<std::string> findings;
    for (const DrawerId& d : hardware_.drawers()) {
        std::string why;
        if (!drawerHealthy(d, expectedCounts, &why)) findings.push_back("Drawer " + d + ": " + why + ".");
    }
    return findings;
}

std::vector<Fault> FaultDetector::selfTest(const std::map<DrawerId, int>& expectedCounts) {
    std::vector<Fault> failed;
    for (const DrawerId& d : hardware_.drawers()) {
        std::string why;
        if (drawerHealthy(d, expectedCounts, &why)) continue;
        raise(FaultType::StartupCheckFailed, d, why, false);
        for (const Fault& f : active_) {
            if (f.type == FaultType::StartupCheckFailed && f.drawer == d) failed.push_back(f);
        }
    }
    return failed;
}

bool FaultDetector::conditionGone(const Fault& fault, const std::map<DrawerId, int>& expectedCounts) {
    switch (fault.type) {
        case FaultType::SensorFailure:
            return hardware_.i_drawerOpen(fault.drawer) && hardware_.i_lockEngaged(fault.drawer) &&
                   hardware_.i_itemCount(fault.drawer);
        case FaultType::UnauthorizedAccess:
        case FaultType::StorageNotClosed: {
            const auto open = hardware_.i_drawerOpen(fault.drawer);
            const auto locked = hardware_.i_lockEngaged(fault.drawer);
            return open && locked && !*open && *locked;
        }
        case FaultType::IncompleteDispense:
        case FaultType::StartupCheckFailed:
            return drawerHealthy(fault.drawer, expectedCounts, nullptr);
        case FaultType::InventoryUpdateFailed:
        case FaultType::StorageFailure:
            return logger_.retryPending();
        case FaultType::CommunicationLoss:
            return false;  // cleared only by CommunicationManager
    }
    return false;
}

void FaultDetector::remove(FaultType type, const DrawerId& drawer) {
    for (auto it = active_.begin(); it != active_.end(); ++it) {
        if (it->type == type && it->drawer == drawer) {
            active_.erase(it);
            return;
        }
    }
}

OpResult FaultDetector::tryClear(const Badge& actor, const std::map<DrawerId, int>& expectedCounts) {
    std::vector<Fault> remaining;
    const std::vector<Fault> snapshot = active_;
    for (const Fault& f : snapshot) {
        if (!f.critical) continue;
        if (conditionGone(f, expectedCounts)) {
            remove(f.type, f.drawer);
            logger_.record(EventType::FAULT_CLEARED, actor, toString(f.type), f.drawer.empty() ? "" : "drawer=" + f.drawer);
        } else {
            remaining.push_back(f);
            logger_.record(EventType::FAULT_CLEAR_DENIED, actor, toString(f.type),
                           (f.drawer.empty() ? std::string() : "drawer=" + f.drawer + " ") + "condition still present");
        }
    }
    if (remaining.empty()) return OpResult::success("All faults are cleared.");
    std::string text = "The fault is still present: " + remaining.front().message;
    if (remaining.size() > 1) text += " (" + std::to_string(remaining.size()) + " faults remain.)";
    return OpResult::failure(ResultCode::FaultStillPresent, text);
}

void FaultDetector::reportCommunicationLoss() {
    report(FaultType::CommunicationLoss, "", "no successful exchange within the loss timeout");
}

void FaultDetector::clearCommunicationLoss() {
    if (!isActive(FaultType::CommunicationLoss, "")) return;
    remove(FaultType::CommunicationLoss, "");
    logger_.record(EventType::FAULT_CLEARED, kSystem, toString(FaultType::CommunicationLoss), "link restored");
}

bool FaultDetector::hasCriticalFault() const {
    for (const Fault& f : active_) {
        if (f.critical) return true;
    }
    return false;
}

std::vector<Fault> FaultDetector::activeFaults() const { return active_; }

bool FaultDetector::isActive(FaultType type, const DrawerId& drawer) const {
    for (const Fault& f : active_) {
        if (f.type == type && f.drawer == drawer) return true;
    }
    return false;
}

void FaultDetector::addListener(Listener listener) { listeners_.push_back(std::move(listener)); }

}  // namespace adc
