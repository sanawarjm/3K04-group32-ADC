#pragma once
// D1 component: Event Logger
// Responsibility:
//   Records dispensing outcomes, authentication attempts, inventory changes, faults, mode
//   changes and maintenance activity in chronological order, and retains them locally.
// D1 user stories:
//   US-LOG-01, US-AUD-01, US-REL-01 (AC-01, AC-02), US-AVL-01 (AC-02)
// D1 requirements traced to this component (D1 section 6):
//   FR-AUTH-004, FR-INV-004, FR-LOG-001, FR-LOG-002, FR-LOG-003, FR-LOG-004, FR-COM-003, FR-
//   COM-005, NFR-REL-001, NFR-REL-002, NFR-AVL-002, NFR-AVL-003, NFR-AUD-001, NFR-AUD-002, NFR-
//   AUD-003, NFR-AUD-004, NFR-AUD-005
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/event_logger.cpp.
//
// ---- Module Interface Specification (MG 1.3.2) --------------------------------
// Responsibility: keeps the single, ordered, append-only audit trail of the cabinet.
// Secret:         the audit record format, how IDs are assigned, and how records are persisted
//                 and retried after a storage failure.
// Uses:           Storage, Clock, types.
// State invariant(s):
//   INV-LOG-1: the records held have IDs 1..n with no gaps, in increasing order.
//   INV-LOG-2: every record ever accepted is held in memory; records not yet on disk are
//              exactly the last unsavedCount() records (nothing is ever dropped, NFR-REL-002).
//   INV-LOG-3: no operation edits, deletes or reorders a record (NFR-AUD-005).
// There is deliberately no edit, delete or clear operation. Queries return copies.

#include <cstdint>
#include <string>
#include <vector>

#include "adc/clock.hpp"
#include "adc/storage.hpp"
#include "adc/types.hpp"

namespace adc {

class EventLogger {
public:
    // EventLogger(storage, clock)
    // Reqs:        NFR-REL-001 (records survive a restart)
    // Pre:         storage and clock outlive the logger.
    // Post:        holds every valid record found in storage; lastId() is the highest ID loaded
    //              (0 if none); healthy() is true.
    // Effects:     reads the "events" file.
    // Exceptions:  none (unreadable lines are skipped by Storage).
    EventLogger(Storage& storage, const Clock& clock);

    // record(type, user, outcome, details) -> id
    // Reqs:        FR-AUTH-004, FR-LOG-002, FR-LOG-003, NFR-AUD-003, NFR-AUD-004
    // Pre:         details contains no PIN and no patient data (caller obligation, R6).
    // Post:        returns lastId(), which is old lastId() + 1; the new record is last in all().
    // Semantics:   timestamp = clock.timestamp(); medication = ""; quantity = 0.
    // Effects:     appends to storage; on failure keeps the record, healthy() becomes false and
    //              the write is retried before the next record (INV-LOG-2).
    // Exceptions:  none.
    std::int64_t record(EventType type, const std::string& user, const std::string& outcome,
                        const std::string& details);

    // recordItem(type, user, medication, quantity, outcome, details) -> id
    // Reqs:        FR-INV-004, NFR-AUD-002
    // Pre/Post/Effects: as record(), with the medication and quantity fields filled.
    std::int64_t recordItem(EventType type, const std::string& user, const MedicationId& medication,
                            int quantity, const std::string& outcome, const std::string& details);

    // recordDispense(user, medication, requested, removed, outcome, reason) -> id
    // Reqs:        FR-LOG-001, NFR-AUD-001, NFR-AUD-002
    // Pre:         outcome is a terminal DispenseState.
    // Post:        one DISPENSE_OUTCOME record with user, medication, quantity = removed, time and
    //              outcome = toString(outcome) all filled; details = "requested=R removed=N reason=...".
    // Effects:     as record().
    std::int64_t recordDispense(const Badge& user, const MedicationId& medication, int requested,
                                int removed, DispenseState outcome, const std::string& reason);

    // recordReplenish(user, medication, added, outcome, reason) -> id
    // Reqs:        FR-INV-003, NFR-AUD-004
    // Post:        one REPLENISH_OUTCOME record; quantity = units added.
    std::int64_t recordReplenish(const Badge& user, const MedicationId& medication, int added,
                                 DispenseState outcome, const std::string& reason);

    // all() -> copy of every record, ordered by ID (NFR-AUD-003).
    std::vector<EventRecord> all() const;

    // after(id, limit) -> copy of the records with ID > id, ascending, at most limit of them.
    // Reqs:        FR-COM-003, FR-COM-005 (CommunicationManager's read cursor)
    // Pre:         limit >= 0.
    std::vector<EventRecord> after(std::int64_t id, int limit) const;

    // recent(n) -> copy of the last n records (fewer if the log is shorter), ascending.
    std::vector<EventRecord> recent(int n) const;

    // lastId() -> ID of the newest record, 0 if none.
    std::int64_t lastId() const;

    // healthy() -> false while any record is not yet saved to storage (FaultDetector raises
    //              StorageFailure from this, NFR-REL-001).
    bool healthy() const;

    // unsavedCount() -> number of records held in memory only.
    int unsavedCount() const;

    // retryPending() -> bool
    // Reqs:        NFR-REL-002
    // Post:        tries to save every unsaved record in order; returns healthy().
    // Effects:     appends to storage.
    bool retryPending();

private:
    std::int64_t append(EventRecord rec);
    void flushPending();

    Storage& storage_;
    const Clock& clock_;
    std::vector<EventRecord> records_;
    std::size_t firstUnsaved_ = 0;  // records_[firstUnsaved_..] are not on disk yet
};

}  // namespace adc
