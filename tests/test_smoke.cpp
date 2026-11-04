#include <gtest/gtest.h>

#include "adc/access_control_manager.hpp"
#include "adc/communication_manager.hpp"
#include "adc/dispensing_controller.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"

// Checks that every D1 component header compiles and can be constructed.
TEST(Smoke, AllComponentsConstruct) {
    EXPECT_NO_THROW({
        [[maybe_unused]] adc::AccessControlManager access;
        [[maybe_unused]] adc::DispensingController dispensing;
        [[maybe_unused]] adc::InventoryManager inventory;
        [[maybe_unused]] adc::EventLogger logger;
        [[maybe_unused]] adc::FaultDetector faults;
        [[maybe_unused]] adc::OperationalModeManager modes;
        [[maybe_unused]] adc::CommunicationManager comms;
        [[maybe_unused]] adc::HardwareInterface hardware;
    });
}
