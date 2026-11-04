#include "adc/console_ui.hpp"

#include <cctype>
#include <charconv>
#include <iomanip>
#include <istream>
#include <ostream>
#include <system_error>

#include "adc/hardware_interface.hpp"

namespace adc {

namespace {

// Column widths of the inventory and event tables (layout only).
constexpr int kIdWidth = 5;
constexpr int kNameWidth = 36;
constexpr int kDrawerWidth = 8;
constexpr int kTypeWidth = 22;

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::optional<int> parseWhole(const std::string& text) {
    int value = 0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, value);
    if (text.empty() || r.ec != std::errc() || r.ptr != end) return std::nullopt;
    return value;
}

}  // namespace

ConsoleUI::ConsoleUI(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

void ConsoleUI::showSuccess(const std::string& message) { out_ << "[OK] " << message << '\n' << std::flush; }

void ConsoleUI::showError(const std::string& message) { out_ << "[ERROR] " << message << '\n' << std::flush; }

void ConsoleUI::showWarning(const std::string& message) { out_ << "WARNING: " << message << '\n' << std::flush; }

void ConsoleUI::showInfo(const std::string& message) { out_ << message << '\n' << std::flush; }

void ConsoleUI::showResult(const OpResult& result) {
    if (result.ok()) {
        if (!result.message.empty()) showSuccess(result.message);
    } else {
        showError(result.message);
    }
}

void ConsoleUI::showCriticalFault(const std::string& message) {
    const IndicatorSignal s = signalFor(IndicatorLevel::Fault);
    out_ << "!!! WARNING: DISPENSING DISABLED - SAFETY FAULT !!!\n"
         << message << '\n'
         << "(indicator: " << s.label << ", " << s.light << " light" << (s.buzzer ? ", buzzer sounding" : "")
         << ")\n"
         << std::flush;
}

void ConsoleUI::showStatusBar(const StatusBarInfo& i) {
    out_ << "\n=== " << i.cabinetId << " | Mode: " << toString(i.mode) << " | Link: " << toString(i.link)
         << " | Unsent: " << i.unsentEvents << " events, " << i.unsentMessages << " messages | Signed in: ";
    if (i.signedIn) {
        out_ << i.userName << " (" << i.role << ") - " << i.secondsLeft << " s left";
    } else {
        out_ << "nobody";
    }
    out_ << " ===\n";
    const IndicatorSignal s = signalFor(i.indicator);
    out_ << "Indicator: [" << s.label << "] " << s.light << " light" << (s.buzzer ? " + buzzer" : "") << " - "
         << i.indicatorText << '\n';
    if (!i.sessionLine.empty()) out_ << "Request: " << i.sessionLine << '\n';
    out_ << std::flush;
}

void ConsoleUI::showInventory(const std::vector<Medication>& medications) {
    out_ << std::left << std::setw(kIdWidth) << "ID" << std::setw(kNameWidth) << "Medication"
         << std::setw(kDrawerWidth) << "Drawer"
         << "Quantity\n";
    for (const Medication& m : medications) {
        out_ << std::left << std::setw(kIdWidth) << m.id << std::setw(kNameWidth) << m.name
             << std::setw(kDrawerWidth) << m.drawer << m.quantity << '\n';
    }
    out_ << std::right << std::flush;
}

void ConsoleUI::showEvents(const std::vector<EventRecord>& records) {
    if (records.empty()) out_ << "(no events)\n";
    for (const EventRecord& r : records) {
        out_ << '#' << r.id << ' ' << r.timestamp << ' ' << std::left << std::setw(kTypeWidth) << toString(r.type)
             << std::right << " user=" << r.user;
        if (!r.medication.empty()) out_ << " med=" << r.medication << " qty=" << r.quantity;
        if (!r.outcome.empty()) out_ << ' ' << r.outcome;
        if (!r.details.empty()) out_ << " | " << r.details;
        out_ << '\n';
    }
    out_ << std::flush;
}

void ConsoleUI::showList(const std::string& title, const std::vector<std::string>& lines) {
    out_ << title << '\n';
    if (lines.empty()) out_ << "  (none)\n";
    for (const std::string& l : lines) out_ << "  " << l << '\n';
    out_ << std::flush;
}

void ConsoleUI::showMenu() {
    out_ << " 1 Sign in                     2 Sign out\n"
            " 3 View inventory              4 Request medication\n"
            " 5 Replenish medication        6 Cancel current request\n"
            " 7 View event log              8 Maintenance: enter / exit / run diagnostics\n"
            " 9 Clear fault\n"
            "10 [TEST HARNESS] Simulated cabinet: open / close / take / add / force open / lock / sensor\n"
            "11 [TEST HARNESS] Simulated network: Monitoring System offline / online / received / config\n"
            "12 Restart ADC (rebuild from storage, sign in again)\n"
            " 0 Exit\n"
         << std::flush;
}

std::optional<std::string> ConsoleUI::readLine(const std::string& prompt) {
    out_ << prompt << std::flush;
    std::string line;
    if (!std::getline(in_, line)) {
        out_ << '\n' << std::flush;
        return std::nullopt;
    }
    return trim(line);
}

MenuInput ConsoleUI::promptMenuChoice(int maxOption) {
    while (true) {
        const auto line = readLine("Choose an option (Enter = refresh): ");
        MenuInput input;
        if (!line) {
            input.endOfInput = true;
            return input;
        }
        if (line->empty()) {
            input.refresh = true;
            return input;
        }
        const auto n = parseWhole(*line);
        if (n && *n >= 0 && *n <= maxOption) {
            input.choice = *n;
            return input;
        }
        showError("Please choose one of the numbers shown (0 to " + std::to_string(maxOption) + ").");
    }
}

std::optional<Credentials> ConsoleUI::promptCredentials() {
    const auto badge = readLine("Badge: ");
    if (!badge) return std::nullopt;
    const auto pin = readLine("PIN: ");
    if (!pin) return std::nullopt;
    std::string upper = *badge;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return Credentials{upper, *pin};
}

std::optional<MedicationId> ConsoleUI::promptMedication() {
    const auto line = readLine("Medication ID (for example M03): ");
    if (!line) return std::nullopt;
    std::string upper = *line;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return upper;
}

std::optional<int> ConsoleUI::promptQuantity() {
    while (true) {
        const auto line = readLine("Quantity: ");
        if (!line) return std::nullopt;
        const auto n = parseWhole(*line);
        if (n && *n >= 1) return n;
        showError("Please enter a whole number of 1 or more.");
    }
}

std::optional<int> ConsoleUI::promptNumber(const std::string& prompt, int min, int max) {
    while (true) {
        const auto line = readLine(prompt);
        if (!line) return std::nullopt;
        const auto n = parseWhole(*line);
        if (n && *n >= min && *n <= max) return n;
        showError("Please enter a number from " + std::to_string(min) + " to " + std::to_string(max) + ".");
    }
}

std::optional<std::string> ConsoleUI::promptText(const std::string& prompt) { return readLine(prompt); }

}  // namespace adc
