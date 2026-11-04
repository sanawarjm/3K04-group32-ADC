#include "adc/fake_monitor.hpp"

#include <set>

namespace adc {

FakeMonitor::FakeMonitor() = default;

TransportResult FakeMonitor::send(const std::vector<std::string>& batch) {
    TransportResult result;
    if (!online_) return result;
    result.delivered = true;
    std::set<std::string> seen;
    for (const ReceivedMessage& m : received_) seen.insert(m.id);
    for (const std::string& json : batch) {
        const auto msg = MessageCodec::parse(json);
        if (!msg) continue;
        const auto id = msg->fields.find("id");
        if (id == msg->fields.end()) continue;
        result.ackedIds.push_back(id->second);
        if (msg->type == "heartbeat") continue;  // acknowledged, not stored
        if (seen.count(id->second)) {
            ++duplicates_;
            continue;
        }
        seen.insert(id->second);
        received_.push_back(ReceivedMessage{id->second, msg->type, json});
    }
    result.inbound = pendingInbound_;
    pendingInbound_.clear();
    return result;
}

void FakeMonitor::setOnline(bool on) { online_ = on; }

bool FakeMonitor::online() const { return online_; }

std::vector<ReceivedMessage> FakeMonitor::received() const { return received_; }

int FakeMonitor::duplicatesDropped() const { return duplicates_; }

void FakeMonitor::queueConfig(const std::string& key, const std::string& value) {
    pendingInbound_.push_back(MessageCodec::configJson("CFG-" + std::to_string(nextConfigId_++), key, value));
}

}  // namespace adc
