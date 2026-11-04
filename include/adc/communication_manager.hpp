#pragma once
// D1 component: Communication Manager
// Responsibility:
//   Sends status, inventory, alarms, maintenance information and events to the Monitoring
//   System, detects loss of communication, keeps operating locally, and resynchronizes when the
//   link returns (D1 State Machine 3).
// D1 user stories:
//   US-COM-01, US-AVL-01, US-PRF-01 (AC-04), US-FAULT-01 (AC-03)
// D1 requirements traced to this component (D1 section 6):
//   FR-LOG-004, FR-FAULT-003, FR-COM-001, FR-COM-002, FR-COM-003, FR-COM-004, FR-COM-005, NFR-
//   REL-002, NFR-AVL-001, NFR-AVL-002, NFR-AVL-003, NFR-AVL-004, NFR-PRF-003
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/communication_manager.cpp.
//
// ---- Module Interface Specification (MG 1.2.4) --------------------------------
// Responsibility: runs D1 State Machine 3 (SM3): keeps the Monitoring System informed, detects
//                 loss of the link, buffers locally while it is down, and resynchronizes.
// Secret:         the link policy: heartbeat and retry period, loss detection (C3), the outbox
//                 (latest-value-wins for status/inventory, never-dropped alarms/maintenance),
//                 the event read cursor, persistence of both, and the resync procedure.
// Uses:           MonitorTransport, MessageCodec, EventLogger (read-only cursor + own records),
//                 Storage, Clock, AdcConfig.
// State invariant(s):
//   INV-COM-1: lastAckedEventId() only increases, and every event with ID <= it was acknowledged
//              (the cursor advances only over a contiguous acknowledged run).
//   INV-COM-2: no event record is ever removed because of the link (events stay in EventLogger;
//              only the cursor moves) - NFR-REL-002.
//   INV-COM-3: alarm and maintenance messages leave the outbox only when acknowledged; at most
//              one status message and one inventory message per medication are queued.
//   INV-COM-4: the outbox and the cursor in storage equal those in memory after every change
//              (NFR-AVL-002).
//   INV-COM-5: no public operation blocks or refuses local cabinet operation (FR-COM-004);
//              requiresMonitoringSystem() is false for every ADC function (C7).
// Upward notification: link listeners (AdcSystem -> FaultDetector::reportCommunicationLoss /
// clearCommunicationLoss). EventLogger never calls this module.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "adc/clock.hpp"
#include "adc/config.hpp"
#include "adc/event_logger.hpp"
#include "adc/message_codec.hpp"
#include "adc/monitor_transport.hpp"
#include "adc/storage.hpp"
#include "adc/types.hpp"

namespace adc {

// One cell of the SM3 table, as text (tests / documentation). A value record.
struct LinkCell {
    std::string kind;  // Go, Stay, Ignore, Illegal
    LinkState target = LinkState::Connecting;
    std::string actions;
};

class CommunicationManager {
public:
    using LinkListener = std::function<void(LinkState from, LinkState to)>;

    // CommunicationManager(transport, logger, storage, clock, config)
    // Reqs:        NFR-AVL-002, NFR-REL-002 (outbox and cursor survive a restart)
    // Pre:         the referenced objects outlive this one.
    // Post:        state() == Connecting; outbox and cursor loaded from storage; the loss timer
    //              starts now.
    CommunicationManager(MonitorTransport& transport, EventLogger& logger, Storage& storage, const Clock& clock,
                         const AdcConfig& config);

    // tick()
    // Reqs:        FR-COM-001..005, FR-FAULT-003, NFR-PRF-003, NFR-AVL-003
    // Pre:         called every tickPeriod.
    // Post:        an exchange was attempted if a heartbeat/retry is due (every heartbeatPeriod)
    //              or, after a successful exchange, while anything is unsent. SM3 events fired:
    //              success in Connecting -> connectionEstablished; success in Disconnected ->
    //              connectionRestored; success with nothing left unsent -> synchronizationComplete;
    //              no success for >= commLossTimeout -> communicationLost (C3).
    // Semantics:   function table COMM transitions (SM3) in docs/specification.md.
    // Effects:     transport.send; acknowledged messages leave the outbox; cursor advances;
    //              COMM_STATE_CHANGED records; CONFIG_UPDATED records (C8); link listeners.
    void tick();

    // publishStatus(mode, criticalFault, reason)
    // Reqs:        FR-COM-001 (status and system health)
    // Post:        exactly one status message is queued (an older unsent one is replaced).
    // Effects:     SM3 outgoingInformation: Connected -> sent now; otherwise kept (queueInformation).
    void publishStatus(Mode mode, bool criticalFault, const std::string& reason);

    // publishInventory(medication)
    // Reqs:        FR-COM-001 (inventory); D1 Replenish activity "send inventory to monitor"
    // Post:        one inventory message for that medication is queued (latest value wins).
    void publishInventory(const Medication& medication);

    // publishAlarm(fault)
    // Reqs:        FR-COM-002 (alarms)
    // Post:        an alarm message is queued; it is never dropped (INV-COM-3).
    void publishAlarm(const Fault& fault);

    // publishMaintenance(activity, by)
    // Reqs:        FR-COM-002 (maintenance information)
    // Post:        a maintenance message is queued; never dropped (INV-COM-3).
    void publishMaintenance(const std::string& activity, const Badge& by);

    // powerOff()
    // Post:        SM3 powerOff: state() == Off; outbox and cursor are saved.
    void powerOff();

    // state() -> current SM3 state.
    LinkState state() const;

    // unsentEventCount() -> events not yet acknowledged; unsentMessageCount() -> outbox size.
    int unsentEventCount() const;
    int unsentMessageCount() const;

    // lastAckedEventId() -> the event cursor (INV-COM-1).
    std::int64_t lastAckedEventId() const;

    // heartbeatPeriod() -> current heartbeat/retry period (changed only by a config update, C8).
    std::chrono::milliseconds heartbeatPeriod() const;

    // timeSinceLastSuccess() -> time since the last successful exchange.
    std::chrono::milliseconds timeSinceLastSuccess() const;

    // requiresMonitoringSystem() -> false (C7: no ADC function needs the Monitoring System).
    bool requiresMonitoringSystem() const;

    // addLinkListener(listener): called after every SM3 state change.
    void addLinkListener(LinkListener listener);

    // transitionFor(state, event) -> the cell [state][event] of the SM3 table. Pure.
    static LinkCell transitionFor(LinkState state, LinkEvent event);

private:
    struct OutboxEntry {
        std::string id;
        std::string type;
        std::string replaceKey;  // "" = never replaced (alarm, maintenance)
        std::string json;
    };

    void fire(LinkEvent event);
    void enter(LinkState state, LinkState from);
    void enqueue(const std::string& type, const std::string& replaceKey, const std::vector<MessageField>& fields);
    void outgoing();
    void attempt();
    void applyConfig(const InboundMessage& msg);
    bool hasPending() const;
    void persist();
    void load();

    MonitorTransport& transport_;
    EventLogger& logger_;
    Storage& storage_;
    const Clock& clock_;
    AdcConfig config_;
    std::chrono::milliseconds heartbeat_;

    LinkState state_ = LinkState::Connecting;
    std::vector<OutboxEntry> outbox_;
    std::int64_t cursor_ = 0;                // last contiguously acknowledged event ID
    std::set<std::int64_t> ackedAhead_;      // acknowledged events above the cursor
    std::int64_t nextMessageId_ = 1;
    std::int64_t nextHeartbeatId_ = 1;
    std::chrono::milliseconds lastSuccess_{0};
    std::chrono::milliseconds lastAttempt_{0};
    bool attempted_ = false;
    bool lastAttemptOk_ = false;
    bool sending_ = false;

    // Latest status / inventory (re-sent in full on resynchronization, FR-COM-005).
    std::vector<MessageField> latestStatus_;
    std::map<MedicationId, std::vector<MessageField>> latestInventory_;
    std::vector<LinkListener> listeners_;
};

}  // namespace adc
