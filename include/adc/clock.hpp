#pragma once
// Time source (not a D1 component; added for D2 so timers can be tested without sleeping).
//
// ---- Module Interface Specification (MG 1.1.5) --------------------------------
// Responsibility: gives the current monotonic time (for timers) and the wall-clock timestamp
//                 (for audit records, NFR-AUD-003).
// Secret:         where time comes from: the real clocks, or a manual clock driven by tests.
// Uses:           nothing (C++ standard library only).
// State invariant(s): INV-CLK-1: now() never decreases.

#include <chrono>
#include <string>

namespace adc {

class Clock {
public:
    virtual ~Clock() = default;

    // now() -> milliseconds
    // Reqs:        NFR-SEC-003, NFR-PRF-002, NFR-PRF-003 (all timers)
    // Pre:         none.
    // Post:        monotonic time since an arbitrary origin; never less than a previous result.
    // Effects:     none.
    // Exceptions:  none.
    virtual std::chrono::milliseconds now() const = 0;

    // timestamp() -> text
    // Reqs:        NFR-AUD-002, NFR-AUD-003
    // Pre:         none.
    // Post:        ISO-8601 UTC time with milliseconds, e.g. "2026-11-03T14:05:09.120Z".
    // Effects:     none.
    // Exceptions:  none.
    virtual std::string timestamp() const = 0;
};

// The real clocks: steady_clock for now(), system_clock for timestamp().
class SystemClock : public Clock {
public:
    // SystemClock()
    // Post:        now() starts at 0 ms.
    SystemClock();
    std::chrono::milliseconds now() const override;
    std::string timestamp() const override;

private:
    std::chrono::steady_clock::time_point origin_;
};

// A clock that only moves when a test (or scenario script) calls advance().
class ManualClock : public Clock {
public:
    // ManualClock()
    // Post:        now() == 0 ms; timestamp() == "1970-01-01T00:00:00.000Z" (Unix epoch + now()).
    ManualClock();

    // advance(step)
    // Pre:         step >= 0 (a negative step is ignored, keeping INV-CLK-1).
    // Post:        now() == old now() + step.
    // Effects:     none besides the clock value.
    void advance(std::chrono::milliseconds step);

    std::chrono::milliseconds now() const override;
    std::string timestamp() const override;

private:
    std::chrono::milliseconds elapsed_{0};
};

// formatUtc(time) -> ISO-8601 UTC text with milliseconds (shared by both clocks).
// Pre:         none.  Post: e.g. "2026-11-03T14:05:09.120Z".  Effects: none.
std::string formatUtc(std::chrono::system_clock::time_point time);

}  // namespace adc
