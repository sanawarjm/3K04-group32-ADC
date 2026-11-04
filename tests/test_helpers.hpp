#pragma once
// Shared test support: a unique temporary data folder per test, deleted afterwards.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

#include "adc/config.hpp"

namespace adc_test {

class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = (std::filesystem::temp_directory_path() /
                 ("adc_test_" + std::to_string(stamp) + "_" + std::to_string(counter++)))
                    .string();
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// A default configuration whose data folder is the given temporary folder.
inline adc::AdcConfig testConfig(const TempDir& dir) {
    adc::AdcConfig cfg;
    cfg.dataDir = dir.path();
    return cfg;
}

}  // namespace adc_test
