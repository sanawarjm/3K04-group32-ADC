#include <gtest/gtest.h>

#include "adc/message_codec.hpp"

using namespace adc;

TEST(MessageCodec, CommonFieldsFirstAndInOrder) {
    OutboundMessage m;
    m.type = "status";
    m.id = "MSG-1";
    m.timestamp = "2026-11-03T10:00:00.000Z";
    m.fields = {{"mode", "NORMAL", false}, {"critical_fault", "false", true}, {"reason", "start", false}};
    EXPECT_EQ(MessageCodec::toJson("ADC-01", m),
              "{\"cabinet_id\":\"ADC-01\",\"type\":\"status\",\"id\":\"MSG-1\",\"timestamp\":\"2026-11-03T10:00:00.000Z\","
              "\"mode\":\"NORMAL\",\"critical_fault\":false,\"reason\":\"start\"}");
}

TEST(MessageCodec, EventFieldNamesMatchInterface) {
    EventRecord r;
    r.id = 7;
    r.timestamp = "t";
    r.type = EventType::DISPENSE_OUTCOME;
    r.user = "N100";
    r.medication = "M03";
    r.quantity = 2;
    r.outcome = "COMPLETED";
    r.details = "requested=2 removed=2";
    const auto json = MessageCodec::toJson("ADC-01", MessageCodec::eventMessage(r));
    for (const char* key : {"\"cabinet_id\"", "\"type\":\"event\"", "\"id\":\"EVT-7\"", "\"timestamp\"",
                            "\"event_id\":7", "\"event_type\":\"DISPENSE_OUTCOME\"", "\"user\":\"N100\"",
                            "\"medication\":\"M03\"", "\"quantity\":2", "\"outcome\":\"COMPLETED\"", "\"details\""})
        EXPECT_NE(json.find(key), std::string::npos) << key;
}

TEST(MessageCodec, Escaping) {
    EXPECT_EQ(MessageCodec::escape("a\"b\\c\nd\re\tf"), "a\\\"b\\\\c\\nd\\re\\tf");
    EXPECT_EQ(MessageCodec::escape(std::string("\x01", 1)), "\\u0001");
    EXPECT_EQ(MessageCodec::escape("Heparin 5,000 units/mL"), "Heparin 5,000 units/mL");
}

TEST(MessageCodec, EscapedTextRoundTrips) {
    OutboundMessage m;
    m.type = "maintenance";
    m.id = "MSG-2";
    m.timestamp = "t";
    m.fields = {{"activity", "quote \" back \\ nl \n tab \t ctl \x02", false}};
    const auto parsed = MessageCodec::parse(MessageCodec::toJson("ADC-01", m));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type, "maintenance");
    EXPECT_EQ(parsed->fields.at("activity"), m.fields[0].value);
    EXPECT_EQ(parsed->fields.at("cabinet_id"), "ADC-01");
}

TEST(MessageCodec, ParsesAck) {
    const auto msg = MessageCodec::parse(MessageCodec::ackJson({"EVT-1", "MSG-4"}));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, "ack");
    EXPECT_EQ(msg->ids, (std::vector<std::string>{"EVT-1", "MSG-4"}));
}

TEST(MessageCodec, ParsesConfig) {
    const auto msg = MessageCodec::parse(MessageCodec::configJson("CFG-1", "heartbeatPeriodMs", "500"));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->type, "config");
    EXPECT_EQ(msg->fields.at("key"), "heartbeatPeriodMs");
    EXPECT_EQ(msg->fields.at("value"), "500");
}

TEST(MessageCodec, ParsesNumbersAndWhitespace) {
    const auto msg = MessageCodec::parse(" { \"type\" : \"x\" , \"n\" : 42 , \"b\" : true } ");
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->fields.at("n"), "42");
    EXPECT_EQ(msg->fields.at("b"), "true");
}

TEST(MessageCodec, RejectsMalformed) {
    for (const char* bad : {"", "{", "[]", "{\"type\":}", "{\"type\":\"x\"", "{\"type\":\"x\"} trailing",
                            "{\"a\":\"no type\"}", "{\"type\":\"bad\\q\"}", "{\"type\":\"x\",}"})
        EXPECT_FALSE(MessageCodec::parse(bad).has_value()) << bad;
}
