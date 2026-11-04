#include "adc/hardware_interface.hpp"

namespace adc {

IndicatorSignal signalFor(IndicatorLevel level) {
    switch (level) {
        case IndicatorLevel::Ok: return IndicatorSignal{"OK", "steady green", false};
        case IndicatorLevel::Warning: return IndicatorSignal{"WARNING", "slow flashing amber", false};
        case IndicatorLevel::Fault: return IndicatorSignal{"FAULT", "fast flashing red", true};
    }
    return IndicatorSignal{"FAULT", "fast flashing red", true};
}

}  // namespace adc
