#include "adc/simulated_cabinet.hpp"

#include <charconv>
#include <system_error>

namespace adc {

namespace {

const char* const kFile = "cabinet";
constexpr std::size_t kFieldCount = 7;  // id, open, engaged, commanded, count, jammed, sensorFailed
const char* const kYes = "1";
const char* const kNo = "0";

const char* flag(bool b) { return b ? kYes : kNo; }

bool parseFlag(const std::string& text, bool& out) {
    if (text == kYes) { out = true; return true; }
    if (text == kNo) { out = false; return true; }
    return false;
}

bool parseInt(const std::string& text, int& out) {
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, out);
    return !text.empty() && r.ec == std::errc() && r.ptr == end;
}

OpResult refused(const std::string& why) { return OpResult::failure(ResultCode::InvalidInput, why); }

}  // namespace

SimulatedCabinet::SimulatedCabinet(Storage& storage, const AdcConfig& config) : storage_(storage) {
    for (const Storage::Row& row : storage_.loadSnapshot(kFile)) {
        DrawerSnapshot d;
        if (row.size() != kFieldCount) continue;
        d.id = row[0];
        if (!parseFlag(row[1], d.open) || !parseFlag(row[2], d.lockEngaged) || !fromString(row[3], d.commanded) ||
            !parseInt(row[4], d.count) || d.count < 0 || !parseFlag(row[5], d.lockJammed) ||
            !parseFlag(row[6], d.sensorFailed))
            continue;
        drawers_.push_back(d);
    }
    if (drawers_.empty()) {
        for (const DrawerId& id : config.drawers) {
            DrawerSnapshot d;
            d.id = id;
            for (const Medication& m : config.seedInventory) {
                if (m.drawer == id) d.count = m.quantity;
            }
            drawers_.push_back(d);
        }
        save();
    }
}

void SimulatedCabinet::save() {
    std::vector<Storage::Row> rows;
    for (const DrawerSnapshot& d : drawers_) {
        rows.push_back({d.id, flag(d.open), flag(d.lockEngaged), toString(d.commanded), std::to_string(d.count),
                        flag(d.lockJammed), flag(d.sensorFailed)});
    }
    storage_.saveSnapshot(kFile, rows);  // a failed save only affects the next restart of the simulator
}

DrawerSnapshot* SimulatedCabinet::find(const DrawerId& d) {
    for (DrawerSnapshot& drawer : drawers_) {
        if (drawer.id == d) return &drawer;
    }
    return nullptr;
}

const DrawerSnapshot* SimulatedCabinet::find(const DrawerId& d) const {
    for (const DrawerSnapshot& drawer : drawers_) {
        if (drawer.id == d) return &drawer;
    }
    return nullptr;
}

void SimulatedCabinet::applyLockPhysics(DrawerSnapshot& drawer) {
    // NAT-4
    if (drawer.commanded == LockCommand::Locked && !drawer.open && !drawer.lockJammed) drawer.lockEngaged = true;
}

std::vector<DrawerId> SimulatedCabinet::drawers() const {
    std::vector<DrawerId> ids;
    for (const DrawerSnapshot& d : drawers_) ids.push_back(d.id);
    return ids;
}

std::optional<bool> SimulatedCabinet::i_drawerOpen(const DrawerId& d) const {
    const DrawerSnapshot* drawer = find(d);
    if (!drawer || drawer->sensorFailed) return std::nullopt;
    return drawer->open;
}

std::optional<bool> SimulatedCabinet::i_lockEngaged(const DrawerId& d) const {
    const DrawerSnapshot* drawer = find(d);
    if (!drawer || drawer->sensorFailed) return std::nullopt;
    return drawer->lockEngaged;
}

std::optional<int> SimulatedCabinet::i_itemCount(const DrawerId& d) const {
    const DrawerSnapshot* drawer = find(d);
    if (!drawer || drawer->sensorFailed) return std::nullopt;
    return drawer->count;
}

void SimulatedCabinet::o_setLock(const DrawerId& d, LockCommand cmd) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return;
    drawer->commanded = cmd;
    if (cmd == LockCommand::Unlocked) drawer->lockEngaged = false;
    applyLockPhysics(*drawer);
    save();
}

void SimulatedCabinet::o_setIndicator(IndicatorLevel level, const std::string& text) {
    indicator_ = level;
    indicatorText_ = text;
}

OpResult SimulatedCabinet::simOpen(const DrawerId& d) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    if (drawer->open) return refused("Drawer " + d + " is already open.");
    if (drawer->lockEngaged) return refused("Drawer " + d + " is locked and does not open.");  // NAT-1
    drawer->open = true;
    save();
    return OpResult::success("Drawer " + d + " opened.");
}

OpResult SimulatedCabinet::simClose(const DrawerId& d) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    if (!drawer->open) return refused("Drawer " + d + " is already closed.");
    drawer->open = false;
    applyLockPhysics(*drawer);
    save();
    return OpResult::success("Drawer " + d + " closed.");
}

OpResult SimulatedCabinet::simForceOpen(const DrawerId& d) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    if (drawer->open) return refused("Drawer " + d + " is already open.");
    drawer->open = true;
    drawer->lockEngaged = false;  // the lock is broken open
    save();
    return OpResult::success("Drawer " + d + " was forced open.");
}

OpResult SimulatedCabinet::simRemove(const DrawerId& d, int n) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    if (n < 1) return refused("Take at least 1 unit.");
    if (!drawer->open) return refused("Drawer " + d + " is closed; open it first.");  // NAT-2
    if (n > drawer->count)
        return refused("Drawer " + d + " only holds " + std::to_string(drawer->count) + " units.");  // NAT-3
    drawer->count -= n;
    save();
    return OpResult::success("Took " + std::to_string(n) + " from " + d + ".");
}

OpResult SimulatedCabinet::simAdd(const DrawerId& d, int n) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    if (n < 1) return refused("Add at least 1 unit.");
    if (!drawer->open) return refused("Drawer " + d + " is closed; open it first.");  // NAT-2
    drawer->count += n;
    save();
    return OpResult::success("Added " + std::to_string(n) + " to " + d + ".");
}

OpResult SimulatedCabinet::simJamLock(const DrawerId& d, bool jammed) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    drawer->lockJammed = jammed;
    applyLockPhysics(*drawer);
    save();
    return OpResult::success(std::string("Lock of ") + d + (jammed ? " jammed." : " repaired."));
}

OpResult SimulatedCabinet::simFailSensor(const DrawerId& d, bool failed) {
    DrawerSnapshot* drawer = find(d);
    if (!drawer) return refused("There is no drawer " + d + ".");
    drawer->sensorFailed = failed;
    save();
    return OpResult::success(std::string("Sensors of ") + d + (failed ? " failed." : " repaired."));
}

CabinetSnapshot SimulatedCabinet::snapshot() const {
    CabinetSnapshot s;
    s.drawers = drawers_;
    s.indicator = indicator_;
    s.indicatorText = indicatorText_;
    return s;
}

}  // namespace adc
