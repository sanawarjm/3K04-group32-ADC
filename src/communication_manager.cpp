#include "adc/communication_manager.hpp"

#include <array>
#include <charconv>
#include <system_error>
#include <utility>

namespace adc {

namespace {

const char* const kFile = "comm";
const char* const kSystem = "ADC";
const char* const kMonitor = "MONITOR";
const char* const kEventPrefix = "EVT-";
const char* const kMessagePrefix = "MSG-";
const char* const kHeartbeatKey = "heartbeatPeriodMs";  // C8 whitelist (the only updatable key)
constexpr std::size_t kCursorFields = 2;
constexpr std::size_t kMessageFields = 5;

// ---- SM3 transition table (D1 State Machine 3; brief section 2.2) ----------------------------
// Go -> change state (entry action runs); Stay -> internal action; sendNow marks the
// Connected/outgoingInformation cell ("send now"); Ignore -> nothing; Illegal -> failSafe.
enum class Kind { Go, Stay, Ignore, Illegal };

struct Cell {
    Kind kind;
    LinkState target;
    bool sendNow;
    const char* actions;
};

constexpr std::size_t kStateCount = 5;  // see LinkState
constexpr std::size_t kEventCount = 6;  // see LinkEvent
using L = LinkState;
using Row = std::array<Cell, kEventCount>;
constexpr Cell ignoreCell(L s) { return Cell{Kind::Ignore, s, false, "ignore"}; }
constexpr Cell queueCell(L s) { return Cell{Kind::Stay, s, false, "queueInformation"}; }
constexpr Cell kOff{Kind::Go, L::Off, false, "shutdown"};
constexpr Cell kIllegal{Kind::Illegal, L::Off, false, "illegal"};

// columns: connectionEstablished, communicationLost, connectionRestored, synchronizationComplete,
//          outgoingInformation, powerOff
const std::array<Row, kStateCount> kTable{{
    /* Connecting */
    Row{Cell{Kind::Go, L::Connected, false, "beginNormalTransmission"},
        Cell{Kind::Go, L::Disconnected, false, "enableLocalBuffering, logCommunicationFault (C4)"},
        ignoreCell(L::Connecting), ignoreCell(L::Connecting), queueCell(L::Connecting), kOff},
    /* Connected */
    Row{Cell{Kind::Ignore, L::Connected, false, "D1: connectionEstablished / ignore"},
        Cell{Kind::Go, L::Disconnected, false, "enableLocalBuffering, logCommunicationFault"},
        ignoreCell(L::Connected), ignoreCell(L::Connected),
        Cell{Kind::Stay, L::Connected, true, "send now"}, kOff},
    /* Disconnected */
    Row{ignoreCell(L::Disconnected), ignoreCell(L::Disconnected),
        Cell{Kind::Go, L::Resynchronizing, false, "beginResynchronization"},
        ignoreCell(L::Disconnected), queueCell(L::Disconnected), kOff},
    /* Resynchronizing */
    Row{ignoreCell(L::Resynchronizing),
        Cell{Kind::Go, L::Disconnected, false, "retainStoredInformation"},
        ignoreCell(L::Resynchronizing),
        Cell{Kind::Go, L::Connected, false, "resumeNormalTransmission"},
        queueCell(L::Resynchronizing), kOff},
    /* Off (D1 final state) */
    Row{kIllegal, kIllegal, kIllegal, kIllegal, kIllegal, Cell{Kind::Ignore, L::Off, false, "ignore"}},
}};

const Cell& cellFor(LinkState s, LinkEvent e) {
    return kTable[static_cast<std::size_t>(s)][static_cast<std::size_t>(e)];
}

bool parseInt64(const std::string& text, std::int64_t& out) {
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, out);
    return !text.empty() && r.ec == std::errc() && r.ptr == end;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

CommunicationManager::CommunicationManager(MonitorTransport& transport, EventLogger& logger, Storage& storage,
                                           const Clock& clock, const AdcConfig& config)
    : transport_(transport),
      logger_(logger),
      storage_(storage),
      clock_(clock),
      config_(config),
      heartbeat_(config.heartbeatPeriod),
      lastSuccess_(clock.now()) {
    load();
}

LinkCell CommunicationManager::transitionFor(LinkState state, LinkEvent event) {
    static const char* const kKindNames[] = {"Go", "Stay", "Ignore", "Illegal"};
    const Cell& c = cellFor(state, event);
    return LinkCell{kKindNames[static_cast<int>(c.kind)], c.target, c.actions};
}

// ---- table driver -------------------------------------------------------------------------------
void CommunicationManager::fire(LinkEvent event) {
    if (static_cast<std::size_t>(state_) >= kStateCount || static_cast<std::size_t>(event) >= kEventCount) return;
    const Cell& cell = cellFor(state_, event);
    switch (cell.kind) {
        case Kind::Go: {
            const LinkState from = state_;
            state_ = cell.target;
            enter(state_, from);
            return;
        }
        case Kind::Stay:
            if (cell.sendNow) attempt();  // Connected: send now
            return;                      // otherwise queueInformation: already queued and saved
        case Kind::Ignore:
            return;
        case Kind::Illegal:
            // failSafe: this module cannot dispense; it records the impossible event and stays Off.
            logger_.record(EventType::UNEXPECTED_EVENT, kSystem, "ILLEGAL",
                           std::string("SM3 state=") + toString(state_) + " event=" + toString(event));
            return;
    }
}

// Entry actions: one place per state.
void CommunicationManager::enter(LinkState state, LinkState from) {
    logger_.record(EventType::COMM_STATE_CHANGED, kSystem, toString(state),
                   std::string(toString(from)) + "->" + toString(state));
    switch (state) {
        case LinkState::Connected:        // beginNormalTransmission / resumeNormalTransmission
        case LinkState::Disconnected:     // enableLocalBuffering / retainStoredInformation (outbox kept)
        case LinkState::Connecting:
            break;
        case LinkState::Resynchronizing:  // beginResynchronization: full status + inventory again
            if (!latestStatus_.empty()) enqueue("status", "status", latestStatus_);
            for (const auto& entry : latestInventory_) enqueue("inventory", "inventory:" + entry.first, entry.second);
            break;
        case LinkState::Off:
            persist();
            break;
    }
    for (const LinkListener& listener : listeners_) listener(from, state);
}

// ---- outbox -------------------------------------------------------------------------------------
void CommunicationManager::enqueue(const std::string& type, const std::string& replaceKey,
                                   const std::vector<MessageField>& fields) {
    if (!replaceKey.empty()) {  // latest value wins (INV-COM-3)
        for (auto it = outbox_.begin(); it != outbox_.end();) {
            it = (it->replaceKey == replaceKey) ? outbox_.erase(it) : it + 1;
        }
    }
    OutboundMessage m;
    m.type = type;
    m.id = kMessagePrefix + std::to_string(nextMessageId_++);
    m.timestamp = clock_.timestamp();
    m.fields = fields;
    outbox_.push_back(OutboxEntry{m.id, type, replaceKey, MessageCodec::toJson(config_.cabinetId, m)});
    persist();  // INV-COM-4
}

void CommunicationManager::outgoing() { fire(LinkEvent::OutgoingInformation); }

void CommunicationManager::publishStatus(Mode mode, bool criticalFault, const std::string& reason) {
    latestStatus_ = {{"mode", toString(mode), false},
                     {"critical_fault", criticalFault ? "true" : "false", true},
                     {"reason", reason, false}};
    enqueue("status", "status", latestStatus_);
    outgoing();
}

void CommunicationManager::publishInventory(const Medication& m) {
    std::vector<MessageField> fields{{"medication", m.id, false},
                                     {"name", m.name, false},
                                     {"drawer", m.drawer, false},
                                     {"quantity", std::to_string(m.quantity), true}};
    latestInventory_[m.id] = fields;
    enqueue("inventory", "inventory:" + m.id, fields);
    outgoing();
}

void CommunicationManager::publishAlarm(const Fault& f) {
    enqueue("alarm", "",
            {{"fault_type", toString(f.type), false},
             {"drawer", f.drawer, false},
             {"critical", f.critical ? "true" : "false", true},
             {"message", f.message, false}});
    outgoing();
}

void CommunicationManager::publishMaintenance(const std::string& activity, const Badge& by) {
    enqueue("maintenance", "", {{"activity", activity, false}, {"user", by, false}});
    outgoing();
}

bool CommunicationManager::hasPending() const { return !outbox_.empty() || logger_.lastId() > cursor_; }

// ---- exchange -----------------------------------------------------------------------------------
void CommunicationManager::attempt() {
    if (sending_ || state_ == LinkState::Off) return;
    sending_ = true;
    const auto now = clock_.now();
    lastAttempt_ = now;
    attempted_ = true;

    std::vector<std::string> batch;
    for (const OutboxEntry& e : outbox_) {
        if (static_cast<int>(batch.size()) >= config_.maxBatchSize) break;
        batch.push_back(e.json);
    }
    const int room = config_.maxBatchSize - static_cast<int>(batch.size());
    for (const EventRecord& r : logger_.after(cursor_, room)) {
        if (ackedAhead_.count(r.id)) continue;
        batch.push_back(MessageCodec::toJson(config_.cabinetId, MessageCodec::eventMessage(r)));
    }
    if (batch.empty()) {
        OutboundMessage hb;
        hb.type = "heartbeat";
        hb.id = "HB-" + std::to_string(nextHeartbeatId_++);
        hb.timestamp = clock_.timestamp();
        batch.push_back(MessageCodec::toJson(config_.cabinetId, hb));
    }

    const TransportResult result = transport_.send(batch);
    if (!result.delivered) {
        lastAttemptOk_ = false;
        sending_ = false;
        return;
    }
    lastAttemptOk_ = true;
    lastSuccess_ = now;

    bool changed = false;
    for (const std::string& id : result.ackedIds) {
        std::int64_t n = 0;
        if (startsWith(id, kEventPrefix) && parseInt64(id.substr(std::string(kEventPrefix).size()), n)) {
            if (n > cursor_) ackedAhead_.insert(n);
        } else if (startsWith(id, kMessagePrefix)) {
            for (auto it = outbox_.begin(); it != outbox_.end(); ++it) {
                if (it->id == id) {
                    outbox_.erase(it);
                    changed = true;
                    break;
                }
            }
        }
    }
    while (ackedAhead_.count(cursor_ + 1)) {  // INV-COM-1: contiguous run only
        ackedAhead_.erase(cursor_ + 1);
        ++cursor_;
        changed = true;
    }
    if (changed) persist();
    for (const std::string& text : result.inbound) {
        const auto msg = MessageCodec::parse(text);
        if (msg && msg->type == "config") applyConfig(*msg);
    }
    sending_ = false;

    fire(state_ == LinkState::Disconnected ? LinkEvent::ConnectionRestored : LinkEvent::ConnectionEstablished);
    if (!hasPending()) fire(LinkEvent::SynchronizationComplete);
}

void CommunicationManager::applyConfig(const InboundMessage& msg) {
    const auto key = msg.fields.find("key");
    const auto value = msg.fields.find("value");
    const std::string k = key == msg.fields.end() ? "" : key->second;
    const std::string v = value == msg.fields.end() ? "" : value->second;
    std::int64_t ms = 0;
    if (k == kHeartbeatKey && parseInt64(v, ms) && ms >= config_.tickPeriod.count() &&
        ms < config_.commLossTimeout.count()) {
        heartbeat_ = std::chrono::milliseconds(ms);
        logger_.record(EventType::CONFIG_UPDATED, kMonitor, "APPLIED", k + "=" + v);
        return;
    }
    const std::string why = k == kHeartbeatKey ? "value out of range" : "unknown key";
    logger_.record(EventType::CONFIG_UPDATED, kMonitor, "REJECTED", "key=" + k + " value=" + v + " reason=" + why);
}

void CommunicationManager::tick() {
    if (state_ == LinkState::Off) return;
    const auto now = clock_.now();
    const bool retryDue = !attempted_ || now - lastAttempt_ >= heartbeat_;
    const bool flushDue = lastAttemptOk_ && hasPending();
    if (retryDue || flushDue) attempt();
    if (clock_.now() - lastSuccess_ >= config_.commLossTimeout) fire(LinkEvent::CommunicationLost);
}

void CommunicationManager::powerOff() { fire(LinkEvent::PowerOff); }

// ---- persistence --------------------------------------------------------------------------------
void CommunicationManager::persist() {
    std::vector<Storage::Row> rows;
    rows.push_back({"cursor", std::to_string(cursor_)});
    rows.push_back({"next", std::to_string(nextMessageId_)});
    for (const OutboxEntry& e : outbox_) rows.push_back({"msg", e.id, e.type, e.replaceKey, e.json});
    storage_.saveSnapshot(kFile, rows);
}

void CommunicationManager::load() {
    for (const Storage::Row& row : storage_.loadSnapshot(kFile)) {
        std::int64_t n = 0;
        if (row.size() == kCursorFields && row[0] == "cursor" && parseInt64(row[1], n)) {
            cursor_ = n;
        } else if (row.size() == kCursorFields && row[0] == "next" && parseInt64(row[1], n)) {
            nextMessageId_ = n;
        } else if (row.size() == kMessageFields && row[0] == "msg") {
            outbox_.push_back(OutboxEntry{row[1], row[2], row[3], row[4]});
        }
    }
}

// ---- queries ------------------------------------------------------------------------------------
LinkState CommunicationManager::state() const { return state_; }

int CommunicationManager::unsentEventCount() const {
    const std::int64_t n = logger_.lastId() - cursor_;
    return n > 0 ? static_cast<int>(n) : 0;
}

int CommunicationManager::unsentMessageCount() const { return static_cast<int>(outbox_.size()); }

std::int64_t CommunicationManager::lastAckedEventId() const { return cursor_; }

std::chrono::milliseconds CommunicationManager::heartbeatPeriod() const { return heartbeat_; }

std::chrono::milliseconds CommunicationManager::timeSinceLastSuccess() const { return clock_.now() - lastSuccess_; }

bool CommunicationManager::requiresMonitoringSystem() const { return false; }

void CommunicationManager::addLinkListener(LinkListener listener) { listeners_.push_back(std::move(listener)); }

}  // namespace adc
