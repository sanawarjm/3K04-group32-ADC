#include "adc/clock.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace adc {

namespace {
// Calendar arithmetic constants of struct tm / ISO-8601 (format constants, not tunable limits).
constexpr int kTmYearOrigin = 1900;
constexpr int kMillisPerSecond = 1000;
constexpr int kTwoDigits = 2;
constexpr int kThreeDigits = 3;
constexpr int kFourDigits = 4;
}  // namespace

std::string formatUtc(std::chrono::system_clock::time_point time) {
    const auto sinceEpoch = time.time_since_epoch();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count() % kMillisPerSecond;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::seconds>(sinceEpoch)));
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::setfill('0') << std::setw(kFourDigits) << (utc.tm_year + kTmYearOrigin) << '-'
        << std::setw(kTwoDigits) << (utc.tm_mon + 1) << '-' << std::setw(kTwoDigits) << utc.tm_mday << 'T'
        << std::setw(kTwoDigits) << utc.tm_hour << ':' << std::setw(kTwoDigits) << utc.tm_min << ':'
        << std::setw(kTwoDigits) << utc.tm_sec << '.' << std::setw(kThreeDigits) << millis << 'Z';
    return out.str();
}

SystemClock::SystemClock() : origin_(std::chrono::steady_clock::now()) {}

std::chrono::milliseconds SystemClock::now() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - origin_);
}

std::string SystemClock::timestamp() const { return formatUtc(std::chrono::system_clock::now()); }

ManualClock::ManualClock() = default;

void ManualClock::advance(std::chrono::milliseconds step) {
    if (step.count() > 0) elapsed_ += step;
}

std::chrono::milliseconds ManualClock::now() const { return elapsed_; }

std::string ManualClock::timestamp() const {
    return formatUtc(std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed_)));
}

}  // namespace adc
