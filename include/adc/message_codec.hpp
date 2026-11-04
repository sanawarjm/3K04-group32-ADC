#pragma once
// Wire format of ADC <-> Monitoring System messages (not a D1 component; added for D2).
// The field names follow interface/adc-monitoring-interface.md (Part A proposal).
//
// ---- Module Interface Specification (MG 1.3.4) --------------------------------
// Responsibility: turns messages into text and text into messages.
// Secret:         the wire format: one flat JSON object per message, field spelling, escaping,
//                 and how acknowledgements and configuration updates are written.
// Uses:           types (EventRecord).
// State invariant(s): none (no state).

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "adc/types.hpp"

namespace adc {

// One field of a message. raw == true writes the value without quotes (numbers, true/false).
struct MessageField {
    std::string key;
    std::string value;
    bool raw = false;
};

// One outbound message before encoding. A value record.
struct OutboundMessage {
    std::string type;  // status, inventory, alarm, maintenance, event, heartbeat
    std::string id;    // MSG-<n>, EVT-<event id>, HB-<n>
    std::string timestamp;
    std::vector<MessageField> fields;
};

// One decoded message. String and number values are in `fields` (as text); a string array (the
// "ids" of an ack) is in `ids`. A value record.
struct InboundMessage {
    std::string type;
    std::map<std::string, std::string> fields;
    std::vector<std::string> ids;
};

class MessageCodec {
public:
    // toJson(cabinetId, message) -> text
    // Reqs:        FR-COM-001, FR-COM-002, FR-COM-003
    // Post:        one JSON object: {"cabinet_id":..,"type":..,"id":..,"timestamp":.., <fields in order>}
    //              with every string escaped (escape()).
    static std::string toJson(const std::string& cabinetId, const OutboundMessage& message);

    // eventMessage(record) -> OutboundMessage
    // Reqs:        FR-COM-003, FR-LOG-004
    // Post:        type "event", id "EVT-<record.id>", timestamp = record.timestamp, fields
    //              event_id, event_type, user, medication, quantity, outcome, details.
    static OutboundMessage eventMessage(const EventRecord& record);

    // parse(text) -> message
    // Pre:         none (any text).
    // Post:        a value iff text is one flat JSON object whose values are strings, numbers,
    //              true/false/null or arrays of strings, with a string "type". Empty otherwise.
    // Exceptions:  none; malformed text gives no value.
    static std::optional<InboundMessage> parse(const std::string& text);

    // ackJson(ids) / configJson(id, key, value) -> text
    // Post:        {"type":"ack","ids":[...]} / {"type":"config","id":..,"key":..,"value":..}
    //              (used by the stand-in Monitoring System, FakeMonitor).
    static std::string ackJson(const std::vector<std::string>& ids);
    static std::string configJson(const std::string& id, const std::string& key, const std::string& value);

    // escape(text) -> JSON string body: \" \\ \n \r \t \b \f and \u00XX for other control characters.
    static std::string escape(const std::string& text);
};

}  // namespace adc
