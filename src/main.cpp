// Entry point for the Automated Dispensing Cabinet (Part A, 3K04 Group 32).
// Parses the command line and runs AdcSystem. No logic lives here.
//
//   adc_app [--data-dir <folder>] [--reset] [--session-timeout <seconds>]

#include <charconv>
#include <chrono>
#include <iostream>
#include <string>
#include <system_error>

#include "adc/adc_system.hpp"
#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/storage.hpp"

namespace {

void usage() {
    std::cout << "Usage: adc_app [--data-dir <folder>] [--reset] [--session-timeout <seconds>]\n"
                 "  --data-dir         folder for inventory, event log and cabinet state (default: data)\n"
                 "  --reset            delete the ADC's own files in that folder first (fresh demo)\n"
                 "  --session-timeout  inactivity sign-out time in seconds (default: 60)\n";
}

}  // namespace

int main(int argc, char** argv) {
    adc::AdcConfig config;
    bool reset = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--data-dir" && hasValue) {
            config.dataDir = argv[++i];
        } else if (arg == "--reset") {
            reset = true;
        } else if (arg == "--session-timeout" && hasValue) {
            const std::string text = argv[++i];
            int seconds = 0;
            const auto r = std::from_chars(text.data(), text.data() + text.size(), seconds);
            if (r.ec != std::errc() || r.ptr != text.data() + text.size() || seconds < 1) {
                usage();
                return 2;
            }
            config.sessionTimeout = std::chrono::seconds(seconds);
        } else {
            usage();
            return arg == "--help" ? 0 : 2;
        }
    }
    if (reset && !adc::Storage::resetDirectory(config.dataDir)) {
        std::cerr << "Could not delete every file in " << config.dataDir << "\n";
        return 1;
    }
    std::cout << "ADC starting (3K04 Group 32, Part A). Data folder: " << config.dataDir << "\n";
    adc::SystemClock clock;
    adc::AdcSystem system(config, clock, std::cin, std::cout, true);
    return system.run();
}
