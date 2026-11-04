#include <gtest/gtest.h>

#include "adc/access_control_manager.hpp"
#include "adc/clock.hpp"
#include "adc/communication_manager.hpp"
#include "adc/dispensing_controller.hpp"
#include "adc/event_logger.hpp"
#include "adc/fault_detector.hpp"
#include "adc/hardware_interface.hpp"
#include "adc/inventory_manager.hpp"
#include "adc/operational_mode_manager.hpp"
#include "adc/storage.hpp"
#include "test_helpers.hpp"

// Checks that every D1 component header compiles and can be constructed.
TEST(Smoke, AllComponentsConstruct) {
    adc_test::TempDir dir;
    EXPECT_NO_THROW({
        adc::ManualClock clock;
        adc::Storage storage(dir.path());
        adc::AdcConfig cfg = adc_test::testConfig(dir);
        adc::EventLogger logger(storage, clock);
        adc::AccessControlManager access(logger, clock, cfg);
        [[maybe_unused]] adc::DispensingController dispensing;
        [[maybe_unused]] adc::InventoryManager inventory(storage, logger, access, cfg);
        [[maybe_unused]] adc::FaultDetector faults;
        [[maybe_unused]] adc::OperationalModeManager modes;
        [[maybe_unused]] adc::CommunicationManager comms;
        [[maybe_unused]] adc::HardwareInterface hardware;
    });
}
