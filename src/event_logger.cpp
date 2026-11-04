#include "adc/event_logger.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace adc {

namespace {

const char* const kFile = "events";
constexpr std::size_t kFieldCount = 8;  // id, timestamp, type, user, medication, quantity, outcome, details

Storage::Row toRow(const EventRecord& r) {
    return {std::to_string(r.id), r.timestamp,   toString(r.type), r.user,
            r.medication,         std::to_string(r.quantity), r.outcome,     r.details};
}

bool parseInt64(const std::string& text, std::int64_t& out) {
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out);
    return !text.empty() && result.ec == std::errc() && result.ptr == end;
}

bool fromRow(const Storage::Row& row, EventRecord& r) {
    if (row.size() != kFieldCount) return false;
    std::int64_t qty = 0;
    if (!parseInt64(row[0], r.id) || !parseInt64(row[5], qty)) return false;
    if (!fromString(row[2], r.type)) return false;
    r.timestamp = row[1];
    r.user = row[3];
    r.medication = row[4];
    r.quantity = static_cast<int>(qty);
    r.outcome = row[6];
    r.details = row[7];
    return true;
}

}  // namespace

EventLogger::EventLogger(Storage& storage, const Clock& clock) : storage_(storage), clock_(clock) {
    for (const Storage::Row& row : storage_.loadRecords(kFile)) {
        EventRecord r;
        // Keep INV-LOG-1: accept only the next ID in sequence.
        if (fromRow(row, r) && r.id == static_cast<std::int64_t>(records_.size()) + 1) records_.push_back(r);
    }
    firstUnsaved_ = records_.size();
}

void EventLogger::flushPending() {
    while (firstUnsaved_ < records_.size()) {
        if (!storage_.appendRecord(kFile, toRow(records_[firstUnsaved_]))) return;
        ++firstUnsaved_;
    }
}

std::int64_t EventLogger::append(EventRecord rec) {
    flushPending();  // retry older unsaved records first, keeping file order == ID order
    rec.id = lastId() + 1;
    rec.timestamp = clock_.timestamp();
    records_.push_back(std::move(rec));
    flushPending();
    return records_.back().id;
}

std::int64_t EventLogger::record(EventType type, const std::string& user, const std::string& outcome,
                                 const std::string& details) {
    EventRecord r;
    r.type = type;
    r.user = user;
    r.outcome = outcome;
    r.details = details;
    return append(r);
}

std::int64_t EventLogger::recordItem(EventType type, const std::string& user, const MedicationId& medication,
                                     int quantity, const std::string& outcome, const std::string& details) {
    EventRecord r;
    r.type = type;
    r.user = user;
    r.medication = medication;
    r.quantity = quantity;
    r.outcome = outcome;
    r.details = details;
    return append(r);
}

std::int64_t EventLogger::recordDispense(const Badge& user, const MedicationId& medication, int requested,
                                         int removed, DispenseState outcome, const std::string& reason) {
    return recordItem(EventType::DISPENSE_OUTCOME, user, medication, removed, toString(outcome),
                      "requested=" + std::to_string(requested) + " removed=" + std::to_string(removed) +
                          " reason=" + reason);
}

std::int64_t EventLogger::recordReplenish(const Badge& user, const MedicationId& medication, int added,
                                          DispenseState outcome, const std::string& reason) {
    return recordItem(EventType::REPLENISH_OUTCOME, user, medication, added, toString(outcome),
                      "added=" + std::to_string(added) + " reason=" + reason);
}

std::vector<EventRecord> EventLogger::all() const { return records_; }

std::vector<EventRecord> EventLogger::after(std::int64_t id, int limit) const {
    std::vector<EventRecord> out;
    if (id < 0) id = 0;
    for (std::size_t i = static_cast<std::size_t>(id); i < records_.size(); ++i) {
        if (static_cast<int>(out.size()) >= limit) break;
        out.push_back(records_[i]);  // INV-LOG-1: records_[i].id == i + 1
    }
    return out;
}

std::vector<EventRecord> EventLogger::recent(int n) const {
    if (n <= 0) return {};
    const std::size_t count = std::min(records_.size(), static_cast<std::size_t>(n));
    return std::vector<EventRecord>(records_.end() - static_cast<std::ptrdiff_t>(count), records_.end());
}

std::int64_t EventLogger::lastId() const {
    return records_.empty() ? 0 : records_.back().id;
}

bool EventLogger::healthy() const { return firstUnsaved_ == records_.size(); }

int EventLogger::unsavedCount() const { return static_cast<int>(records_.size() - firstUnsaved_); }

bool EventLogger::retryPending() {
    flushPending();
    return healthy();
}

}  // namespace adc
