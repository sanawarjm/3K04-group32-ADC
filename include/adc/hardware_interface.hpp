#pragma once
// Boundary: Cabinet hardware (the "Hardware" swimlane in the D1 activity diagrams)
// D1 scope (section 1.2): drawers, locks, sensors and actuators already exist and
// provide software interfaces the ADC communicates with. Their mechanical design is
// out of scope.
// This header is where the ADC meets that hardware: drawer access (unlock / relock),
// removal and quantity sensing, and the local warning indicators. For the D2 demo a
// simulated implementation can sit behind the same interface.
// D1 requirements that depend on it: FR-DISP-003, FR-DISP-004, FR-FAULT-002,
// NFR-REL-004, NFR-USB-003.
//
// ---- Module Interface Specification (MG 1.1.1) --------------------------------
// Responsibility: the software inputs (i_*) and outputs (o_*) of the 4-variable model.
// Secret:         how drawers, locks, sensors and indicators are actually driven.
// Uses:           types.
// State invariant(s): none at this level (each implementation documents its own).
//
// 4-variable model (docs/specification.md):
//   monitored m_drawerOpen(d), m_lockEngaged(d), m_itemCount(d)  -> IN  -> i_drawerOpen(d),
//             i_lockEngaged(d), i_itemCount(d)  (no value = the sensor has failed)
//   o_setLock(d, cmd), o_setIndicator(level, text) -> OUT -> controlled c_lock(d),
//             c_light, c_buzzer, c_display

#include <optional>
#include <string>
#include <vector>

#include "adc/types.hpp"

namespace adc {

// How an indicator level is shown (NFR-USB-003: text label + light pattern, never colour alone).
struct IndicatorSignal {
    const char* label;     // "OK", "WARNING", "FAULT"
    const char* light;     // "steady green", "slow flashing amber", "fast flashing red"
    bool buzzer;           // true only for FAULT
};

// signalFor(level) -> IndicatorSignal
// Reqs:        NFR-USB-003
// Post:        OK -> {OK, steady green, no buzzer}; WARNING -> {WARNING, slow flashing amber, no
//              buzzer}; FAULT -> {FAULT, fast flashing red, buzzer}.
IndicatorSignal signalFor(IndicatorLevel level);

class HardwareInterface {
public:
    virtual ~HardwareInterface() = default;

    // drawers() -> identifiers of every drawer, e.g. D1..D4.
    virtual std::vector<DrawerId> drawers() const = 0;

    // i_drawerOpen(d) -> true if drawer d is open; empty if its sensor gives no reading or d is unknown.
    // Reqs:        FR-DISP-004, FR-FAULT-002
    virtual std::optional<bool> i_drawerOpen(const DrawerId& d) const = 0;

    // i_lockEngaged(d) -> true if the lock of d is engaged; empty on sensor failure / unknown d.
    // Reqs:        FR-DISP-003, US-FAULT-01 AC-02
    virtual std::optional<bool> i_lockEngaged(const DrawerId& d) const = 0;

    // i_itemCount(d) -> units sensed in d; empty on sensor failure / unknown d.
    // Reqs:        FR-DISP-004, FR-INV-002, NFR-PRF-002
    virtual std::optional<int> i_itemCount(const DrawerId& d) const = 0;

    // o_setLock(d, cmd)
    // Reqs:        FR-DISP-003, NFR-SAF-001
    // Post:        c_lock(d) is commanded to cmd. A LOCKED command engages when the drawer is
    //              closed (and the lock is not jammed); it stays commanded until changed.
    // Exceptions:  an unknown d is ignored.
    virtual void o_setLock(const DrawerId& d, LockCommand cmd) = 0;

    // o_setIndicator(level, text)
    // Reqs:        NFR-USB-003, FR-FAULT-002
    // Post:        c_light / c_buzzer show signalFor(level); c_display shows text.
    virtual void o_setIndicator(IndicatorLevel level, const std::string& text) = 0;
};

}  // namespace adc
