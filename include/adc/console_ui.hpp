#pragma once
// Local screen and keyboard of the cabinet (not a D1 component; added for D2). Presentation
// only: it decides nothing about safety or permissions.
//
// ---- Module Interface Specification (MG 1.2.5) --------------------------------
// Responsibility: shows messages, the status bar, lists and the menu; reads and checks input.
// Secret:         the screen wording, layout and input handling (NFR-USB-001..004).
// Uses:           types, HardwareInterface (signalFor, to word the indicator).
// State invariant(s):
//   INV-UI-1: no output contains an ANSI escape character (NFR-USB-003: no colour codes at all).
//   INV-UI-2: a PIN read by promptCredentials() is never written to the output.
// Every prompt returns no value at end of input (EOF), so the program can exit cleanly.

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "adc/types.hpp"

namespace adc {

// What the status bar shows. A value record filled by AdcSystem.
struct StatusBarInfo {
    std::string cabinetId;
    Mode mode = Mode::Startup;
    LinkState link = LinkState::Connecting;
    int unsentEvents = 0;
    int unsentMessages = 0;
    bool signedIn = false;
    std::string userName;
    std::string role;
    int secondsLeft = 0;
    IndicatorLevel indicator = IndicatorLevel::Ok;
    std::string indicatorText;
    std::string sessionLine;  // empty when no drawer access is in progress
};

// Badge and PIN as typed. A value record; the PIN is passed straight to authentication.
struct Credentials {
    Badge badge;
    std::string pin;
};

// One answer to the main menu. A value record.
struct MenuInput {
    bool endOfInput = false;  // EOF: exit cleanly
    bool refresh = false;     // empty line: draw the status bar again
    int choice = 0;
};

class ConsoleUI {
public:
    // ConsoleUI(in, out)
    // Pre:         in and out outlive this object.
    ConsoleUI(std::istream& in, std::ostream& out);

    // showSuccess / showError / showWarning / showInfo (message)
    // Reqs:        NFR-USB-001, NFR-USB-002, NFR-USB-004
    // Post:        one line "[OK] msg" / "[ERROR] msg" / "WARNING: msg" / "msg" was written.
    void showSuccess(const std::string& message);
    void showError(const std::string& message);
    void showWarning(const std::string& message);
    void showInfo(const std::string& message);

    // showResult(result): showSuccess(message) if result.ok() (and message not empty), else showError.
    void showResult(const OpResult& result);

    // showCriticalFault(message)
    // Reqs:        NFR-USB-003, FR-FAULT-002, NFR-SAF-001
    // Post:        "!!! WARNING: DISPENSING DISABLED - SAFETY FAULT !!!", the message, and the
    //              light/buzzer wording were written (text, not colour).
    void showCriticalFault(const std::string& message);

    // showStatusBar(info)
    // Post:        "=== <id> | Mode: M | Link: L | Unsent: E events, N messages | Signed in: ... ==="
    //              then an "Indicator:" line with label, light pattern and buzzer, then the
    //              request line if any.
    void showStatusBar(const StatusBarInfo& info);

    // showInventory(medications): one row per medication (id, name, drawer, quantity). FR-INV-001
    void showInventory(const std::vector<Medication>& medications);

    // showEvents(records): one row per record, oldest first. NFR-AUD-003
    void showEvents(const std::vector<EventRecord>& records);

    // showList(title, lines): a titled list (harness views).
    void showList(const std::string& title, const std::vector<std::string>& lines);

    // showMenu(): every option, always (so denials can be demonstrated).
    void showMenu();

    // promptMenuChoice(maxOption) -> MenuInput
    // Post:        endOfInput at EOF; refresh for an empty line; otherwise a choice in
    //              0..maxOption (other input is rejected with an error and asked again).
    MenuInput promptMenuChoice(int maxOption);

    // promptCredentials() -> badge and PIN; empty at EOF. INV-UI-2: the PIN is not echoed back.
    std::optional<Credentials> promptCredentials();

    // promptMedication() -> medication ID as typed (trimmed, upper case); empty at EOF.
    std::optional<MedicationId> promptMedication();

    // promptQuantity() -> a whole number >= 1; other input is rejected and asked again; empty at EOF.
    std::optional<int> promptQuantity();

    // promptNumber(prompt, min, max) -> a whole number in [min, max]; asked again otherwise; empty at EOF.
    std::optional<int> promptNumber(const std::string& prompt, int min, int max);

    // promptText(prompt) -> one trimmed line; empty at EOF.
    std::optional<std::string> promptText(const std::string& prompt);

private:
    std::optional<std::string> readLine(const std::string& prompt);

    std::istream& in_;
    std::ostream& out_;
};

}  // namespace adc
