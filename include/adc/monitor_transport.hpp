#pragma once
// How bytes reach the Monitoring System (not a D1 component; added for D2). The real transport
// is to be agreed with Part B (interface/adc-monitoring-interface.md, open questions).
//
// ---- Module Interface Specification (MG 1.1.4) --------------------------------
// Responsibility: delivers one batch of encoded messages and returns what came back.
// Secret:         the transport (TCP, HTTP, MQTT, ... - to agree with Part B).
// Uses:           nothing.
// State invariant(s): none at this level.

#include <string>
#include <vector>

namespace adc {

// Result of one exchange. A value record.
struct TransportResult {
    bool delivered = false;               // false: no successful exchange (link down)
    std::vector<std::string> ackedIds;    // message IDs the Monitoring System confirmed
    std::vector<std::string> inbound;     // encoded messages sent back (e.g. config updates)
};

class MonitorTransport {
public:
    virtual ~MonitorTransport() = default;

    // send(batch) -> TransportResult
    // Reqs:        FR-COM-001..003, FR-COM-005, NFR-PRF-003
    // Pre:         each element is one encoded message (MessageCodec::toJson).
    // Post:        delivered == true iff the exchange succeeded; then ackedIds lists the
    //              confirmed IDs and inbound the messages sent back.
    // Exceptions:  none; a failure is delivered == false. Must never block local operation.
    virtual TransportResult send(const std::vector<std::string>& batch) = 0;
};

}  // namespace adc
