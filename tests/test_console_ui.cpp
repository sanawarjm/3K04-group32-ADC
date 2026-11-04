#include <gtest/gtest.h>

#include <sstream>

#include "adc/console_ui.hpp"

using namespace adc;

class UiTest : public ::testing::Test {
protected:
    std::istringstream in;
    std::ostringstream out;
    ConsoleUI ui{in, out};
    void input(const std::string& text) {
        in.clear();
        in.str(text);
    }
};

TEST_F(UiTest, Prefixes) {
    ui.showSuccess("done");
    ui.showError("no");
    ui.showWarning("careful");
    EXPECT_EQ(out.str(), "[OK] done\n[ERROR] no\nWARNING: careful\n");
}

TEST_F(UiTest, ShowResultChoosesPrefix) {
    ui.showResult(OpResult::success("fine"));
    ui.showResult(OpResult::failure(ResultCode::PermissionDenied, "You are not authorised to x."));
    EXPECT_EQ(out.str(), "[OK] fine\n[ERROR] You are not authorised to x.\n");
}

TEST_F(UiTest, CriticalFaultText_NFR_USB_003) {
    ui.showCriticalFault("A drawer did not lock (drawer D3). Dispensing is stopped. Call a technician.");
    const std::string s = out.str();
    EXPECT_EQ(s.rfind("!!! WARNING: DISPENSING DISABLED - SAFETY FAULT !!!\n", 0), 0u);
    EXPECT_NE(s.find("A drawer did not lock"), std::string::npos);
    EXPECT_NE(s.find("buzzer"), std::string::npos);
    EXPECT_NE(s.find("fast flashing red"), std::string::npos);
}

TEST_F(UiTest, INV_UI_1_NoAnsiEscapeAnywhere) {
    StatusBarInfo info;
    info.cabinetId = "ADC-01";
    info.indicator = IndicatorLevel::Fault;
    info.indicatorText = "x";
    ui.showStatusBar(info);
    ui.showCriticalFault("x");
    ui.showWarning("x");
    ui.showMenu();
    ui.showInventory({Medication{"M01", "A", "D1", 1}});
    EventRecord r;
    ui.showEvents({r});
    EXPECT_EQ(out.str().find('\x1b'), std::string::npos);
}

TEST_F(UiTest, StatusBarFormat) {
    StatusBarInfo info;
    info.cabinetId = "ADC-01";
    info.mode = Mode::Normal;
    info.link = LinkState::Connected;
    info.signedIn = true;
    info.userName = "Nurse Alex Chen";
    info.role = "Nurse";
    info.secondsLeft = 55;
    info.indicatorText = "Ready.";
    ui.showStatusBar(info);
    EXPECT_NE(out.str().find("=== ADC-01 | Mode: NORMAL | Link: CONNECTED | Unsent: 0 events, 0 messages | "
                             "Signed in: Nurse Alex Chen (Nurse) - 55 s left ==="),
              std::string::npos);
    EXPECT_NE(out.str().find("Indicator: [OK] steady green light - Ready."), std::string::npos);
}

TEST_F(UiTest, INV_UI_2_PinNotEchoed) {
    input("n100\n4321\n");
    const auto cred = ui.promptCredentials();
    ASSERT_TRUE(cred.has_value());
    EXPECT_EQ(cred->badge, "N100");
    EXPECT_EQ(cred->pin, "4321");
    EXPECT_EQ(out.str().find("4321"), std::string::npos);
}

TEST_F(UiTest, BadQuantityRejectedThenAccepted) {
    input("abc\n0\n-2\n2.5\n3\n");
    const auto q = ui.promptQuantity();
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(*q, 3);
    std::size_t errors = 0;
    for (std::size_t p = out.str().find("[ERROR]"); p != std::string::npos; p = out.str().find("[ERROR]", p + 1)) ++errors;
    EXPECT_EQ(errors, 4u);
}

TEST_F(UiTest, MenuChoiceValidation) {
    input("99\nx\n\n4\n");
    MenuInput m = ui.promptMenuChoice(12);
    EXPECT_TRUE(m.refresh);  // the empty line, after two rejected inputs
    m = ui.promptMenuChoice(12);
    EXPECT_EQ(m.choice, 4);
    EXPECT_NE(out.str().find("[ERROR] Please choose one of the numbers shown"), std::string::npos);
}

TEST_F(UiTest, EofEndsEveryPromptCleanly) {
    input("");
    EXPECT_TRUE(ui.promptMenuChoice(12).endOfInput);
    EXPECT_FALSE(ui.promptCredentials().has_value());
    EXPECT_FALSE(ui.promptMedication().has_value());
    EXPECT_FALSE(ui.promptQuantity().has_value());
    EXPECT_FALSE(ui.promptNumber("n: ", 1, 3).has_value());
    EXPECT_FALSE(ui.promptText("t: ").has_value());
}

TEST_F(UiTest, MedicationUpperCasedAndTrimmed) {
    input("  m03 \n");
    EXPECT_EQ(ui.promptMedication(), MedicationId("M03"));
}

TEST_F(UiTest, InventoryAndEventsShown) {
    ui.showInventory({Medication{"M03", "Morphine 2 mg/mL injection", "D3", 8}});
    EventRecord r;
    r.id = 3;
    r.timestamp = "t";
    r.type = EventType::DISPENSE_OUTCOME;
    r.user = "N100";
    r.medication = "M03";
    r.quantity = 2;
    r.outcome = "COMPLETED";
    ui.showEvents({r});
    const std::string s = out.str();
    EXPECT_NE(s.find("Morphine 2 mg/mL injection"), std::string::npos);
    EXPECT_NE(s.find("#3 t DISPENSE_OUTCOME"), std::string::npos);
    EXPECT_NE(s.find("COMPLETED"), std::string::npos);
}
