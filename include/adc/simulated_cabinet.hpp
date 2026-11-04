#pragma once
// Simulated cabinet hardware for the D2 demo and tests (not a D1 component). It stands in for the
// D1 "Hardware" swimlane behind HardwareInterface, so ADC logic cannot tell it from real hardware.
//
// ---- Module Interface Specification (MG 1.1.2) --------------------------------
// Responsibility: models the physical cabinet: the monitored variables m_drawerOpen(d),
//                 m_lockEngaged(d), m_itemCount(d) and the controlled variables c_lock(d),
//                 c_light, c_buzzer, c_display, plus fault injection for the demo.
// Secret:         the stand-in physics (what a person at the cabinet can and cannot do) and
//                 how the physical state is persisted between runs.
// Uses:           HardwareInterface, Storage, AdcConfig, types.
// State invariant(s) (NAT constraints, docs/specification.md):
//   NAT-1: a drawer opens only while its lock is disengaged, unless forced open (tamper).
//   NAT-2: the item count changes only while the drawer is open.
//   NAT-3: the item count is never below 0.
//   NAT-4: the lock engages only when the drawer is closed, the lock is commanded LOCKED and
//          the lock is not jammed.
//
// The sim* operations are the test harness ("the physical world"). ADC logic never calls them;
// only tests, AdcSystem's harness menu (options 10) and this class do.

#include <map>
#include <string>
#include <vector>

#include "adc/config.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/storage.hpp"
#include "adc/types.hpp"

namespace adc {

// One drawer as seen by an observer standing at the cabinet. A value record.
struct DrawerSnapshot {
    DrawerId id;
    bool open = false;
    bool lockEngaged = true;
    LockCommand commanded = LockCommand::Locked;
    int count = 0;
    bool lockJammed = false;
    bool sensorFailed = false;
};

// The whole cabinet as seen by an observer. A value record.
struct CabinetSnapshot {
    std::vector<DrawerSnapshot> drawers;
    IndicatorLevel indicator = IndicatorLevel::Ok;
    std::string indicatorText;
};

class SimulatedCabinet : public HardwareInterface {
public:
    // SimulatedCabinet(storage, config)
    // Reqs:        NFR-REL-004 (state survives a restart so the start-up check is meaningful)
    // Pre:         storage outlives this object.
    // Post:        physical state loaded from the "cabinet" file; on the first run every drawer of
    //              config.drawers is closed, locked and holds its seedInventory quantity.
    // Effects:     may write the "cabinet" file.
    SimulatedCabinet(Storage& storage, const AdcConfig& config);

    std::vector<DrawerId> drawers() const override;
    std::optional<bool> i_drawerOpen(const DrawerId& d) const override;
    std::optional<bool> i_lockEngaged(const DrawerId& d) const override;
    std::optional<int> i_itemCount(const DrawerId& d) const override;
    void o_setLock(const DrawerId& d, LockCommand cmd) override;
    void o_setIndicator(IndicatorLevel level, const std::string& text) override;

    // ---- simulator-only controls (test harness) ----------------------------------------------
    // Each returns Ok, or InvalidInput with a plain reason when NAT-1..4 forbid the action or d is
    // unknown; a refused action changes nothing. Every accepted change is persisted.

    // simOpen(d): a person pulls the drawer. Refused while the lock is engaged (NFR: NAT-1).
    OpResult simOpen(const DrawerId& d);
    // simClose(d): a person pushes the drawer shut; the lock engages if commanded LOCKED (NAT-4).
    OpResult simClose(const DrawerId& d);
    // simForceOpen(d): tamper - the drawer opens even though it is locked; the lock disengages.
    OpResult simForceOpen(const DrawerId& d);
    // simRemove(d, n): a person takes n units. Requires open (NAT-2), n >= 1, n <= count (NAT-3).
    OpResult simRemove(const DrawerId& d, int n);
    // simAdd(d, n): a person puts n units in. Requires open (NAT-2), n >= 1.
    OpResult simAdd(const DrawerId& d, int n);
    // simJamLock(d, jammed): jam (true) or repair (false) the lock; a repaired lock engages if
    //                        commanded LOCKED and the drawer is closed.
    OpResult simJamLock(const DrawerId& d, bool jammed);
    // simFailSensor(d, failed): break (true) or repair (false) every sensor of drawer d.
    OpResult simFailSensor(const DrawerId& d, bool failed);

    // snapshot() -> copy of the physical state and the indicator.
    CabinetSnapshot snapshot() const;

private:
    DrawerSnapshot* find(const DrawerId& d);
    const DrawerSnapshot* find(const DrawerId& d) const;
    void applyLockPhysics(DrawerSnapshot& drawer);
    void save();

    Storage& storage_;
    std::vector<DrawerSnapshot> drawers_;
    IndicatorLevel indicator_ = IndicatorLevel::Ok;
    std::string indicatorText_;
};

}  // namespace adc
