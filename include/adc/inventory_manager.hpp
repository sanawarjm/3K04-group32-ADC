#pragma once
// D1 component: Inventory Manager
// Responsibility:
//   Maintains the recorded quantity and storage location of each medication, and updates it on
//   dispensing and replenishment.
// D1 user stories:
//   US-INV-01, US-REL-01 (AC-01, AC-05), US-PRF-01 (AC-02)
// D1 requirements traced to this component (D1 section 6):
//   FR-INV-001, FR-INV-002, FR-INV-003, FR-INV-004, FR-DISP-002, NFR-REL-001, NFR-REL-003, NFR-
//   PRF-002
//
// D2: document the exported operations below (inputs, outputs, semantics,
// pre/post-conditions, effects, exceptions). Keep implementation details in
// src/inventory_manager.cpp.
//
// ---- Module Interface Specification (MG 1.3.1) --------------------------------
// Responsibility: owns the medication records (name, drawer, recorded quantity).
// Secret:         how records are stored, the medication -> drawer map, and the
//                 "persist first, then report" update rule.
// Uses:           Storage, EventLogger, AccessControlManager (Replenish permission), AdcConfig.
// State invariant(s):
//   INV-INV-1: every recorded quantity is >= 0.
//   INV-INV-2: the in-memory records equal the last successfully saved snapshot (an update that
//              cannot be saved is rolled back).
//   INV-INV-3: each drawer holds at most one medication.

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "adc/access_control_manager.hpp"
#include "adc/config.hpp"
#include "adc/event_logger.hpp"
#include "adc/storage.hpp"
#include "adc/types.hpp"

namespace adc {

class InventoryManager {
public:
    using ChangeListener = std::function<void(const Medication&)>;

    // InventoryManager(storage, logger, access, config)
    // Reqs:        NFR-REL-001
    // Pre:         the referenced objects outlive this one.
    // Post:        records are loaded from storage; on the very first run (no inventory file)
    //              they are seeded from config.seedInventory and saved.
    // Effects:     may write the "inventory" file.
    InventoryManager(Storage& storage, EventLogger& logger, AccessControlManager& access, const AdcConfig& config);

    // hasMedication(id) -> true iff id is in the drug library.
    bool hasMedication(const MedicationId& id) const;

    // quantityOf(id) -> recorded quantity (FR-INV-001); 0 for an unknown id.
    int quantityOf(const MedicationId& id) const;

    // locationOf(id) -> drawer holding id (FR-DISP-002); empty for an unknown id.
    std::optional<DrawerId> locationOf(const MedicationId& id) const;

    // medicationAt(drawer) -> medication stored in drawer; empty if none.
    std::optional<MedicationId> medicationAt(const DrawerId& drawer) const;

    // nameOf(id) -> display name; the id itself if unknown.
    std::string nameOf(const MedicationId& id) const;

    // medications() -> copy of every record, ordered by id (FR-INV-001).
    std::vector<Medication> medications() const;

    // expectedCounts() -> recorded quantity per drawer (used by FaultDetector checks).
    std::map<DrawerId, int> expectedCounts() const;

    // canDispense(id, quantity) -> OpResult
    // Reqs:        FR-DISP-002, US-DISP-01 AC-02
    // Post:        UnknownMedication if !hasMedication(id); InvalidQuantity if quantity < 1;
    //              InsufficientStock if quantityOf(id) < quantity; Ok otherwise. No state change.
    OpResult canDispense(const MedicationId& id, int quantity) const;

    // recordRemoval(id, n, by, reason) -> OpResult
    // Reqs:        FR-INV-002, FR-INV-004, NFR-REL-003, NFR-PRF-002
    // Pre:         called only after the removal was sensed (DispensingController).
    // Post:        Ok: quantityOf(id) == old - n and the new value is saved.
    //              StorageError: nothing changed (rolled back, INV-INV-2); the caller raises
    //              InventoryUpdateFailed. UnknownMedication / InvalidQuantity (n < 1 or n > old,
    //              INV-INV-1): nothing changed.
    // Effects:     saves "inventory"; INVENTORY_CHANGED record; change listeners notified.
    OpResult recordRemoval(const MedicationId& id, int n, const Badge& by, const std::string& reason);

    // recordAddition(id, n, by, reason) -> OpResult
    // Reqs:        FR-INV-003, FR-INV-004, NFR-REL-003
    // Post/Effects: as recordRemoval with quantityOf(id) == old + n.
    OpResult recordAddition(const MedicationId& id, int n, const Badge& by, const std::string& reason);

    // replenishAuthorized() -> OpResult
    // Reqs:        FR-INV-003, FR-AUTH-003
    // Post:        AccessControlManager::authorize(Replenish, "replenish medication").
    // Effects:     ACCESS_DENIED record on denial.
    OpResult replenishAuthorized();

    // addChangeListener(listener)
    // Post:        listener is called with the new record after every saved change.
    void addChangeListener(ChangeListener listener);

private:
    OpResult change(const MedicationId& id, int delta, const Badge& by, const std::string& reason);
    bool save();

    Storage& storage_;
    EventLogger& logger_;
    AccessControlManager& access_;
    std::map<MedicationId, Medication> records_;
    std::vector<ChangeListener> listeners_;
};

}  // namespace adc
