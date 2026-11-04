#pragma once
// D2 stand-in for the Monitoring System (Part B). This is NOT Part B's code: it only lets the
// ADC be demonstrated and tested without the real partner system.
//
// ---- Module Interface Specification (MG 1.1.4) --------------------------------
// Responsibility: behaves like a Monitoring System at the far end of MonitorTransport.
// Secret:         how the stand-in stores what it receives and when it answers.
// Uses:           MonitorTransport, MessageCodec.
// State invariant(s):
//   INV-FAKE-1: received() holds each message ID at most once, in arrival order; a repeated ID
//               is counted in duplicatesDropped() instead.

#include <string>
#include <vector>

#include "adc/message_codec.hpp"
#include "adc/monitor_transport.hpp"

namespace adc {

// One message as the stand-in stored it. A value record.
struct ReceivedMessage {
    std::string id;
    std::string type;
    std::string json;
};

class FakeMonitor : public MonitorTransport {
public:
    // FakeMonitor()
    // Post:        online(); nothing received.
    FakeMonitor();

    // send(batch): while online, stores every new message ID, drops (and counts) duplicates,
    //              acknowledges every ID and hands back queued config messages. While offline,
    //              returns delivered == false and stores nothing.
    TransportResult send(const std::vector<std::string>& batch) override;

    // setOnline(on) / online(): simulate the Monitoring System being reachable or not.
    void setOnline(bool on);
    bool online() const;

    // received() -> copy of the stored messages, in arrival order (INV-FAKE-1).
    std::vector<ReceivedMessage> received() const;

    // duplicatesDropped() -> number of repeated IDs dropped.
    int duplicatesDropped() const;

    // queueConfig(key, value): a configuration update (clarification C8) delivered with the
    //                          next successful exchange.
    void queueConfig(const std::string& key, const std::string& value);

private:
    bool online_ = true;
    std::vector<ReceivedMessage> received_;
    std::vector<std::string> pendingInbound_;
    int duplicates_ = 0;
    int nextConfigId_ = 1;
};

}  // namespace adc
