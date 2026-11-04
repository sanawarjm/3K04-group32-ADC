#include "adc/inventory_manager.hpp"

#include <charconv>
#include <system_error>
#include <utility>

namespace adc {

namespace {

const char* const kFile = "inventory";
constexpr std::size_t kFieldCount = 4;  // id, name, drawer, quantity

bool parseInt(const std::string& text, int& out) {
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, out);
    return !text.empty() && r.ec == std::errc() && r.ptr == end;
}

}  // namespace

InventoryManager::InventoryManager(Storage& storage, EventLogger& logger, AccessControlManager& access,
                                   const AdcConfig& config)
    : storage_(storage), logger_(logger), access_(access) {
    for (const Storage::Row& row : storage_.loadSnapshot(kFile)) {
        int qty = 0;
        if (row.size() != kFieldCount || !parseInt(row[3], qty) || qty < 0) continue;  // INV-INV-1
        records_[row[0]] = Medication{row[0], row[1], row[2], qty};
    }
    if (records_.empty()) {
        for (const Medication& m : config.seedInventory) records_[m.id] = m;
        save();
    }
}

bool InventoryManager::save() {
    std::vector<Storage::Row> rows;
    for (const auto& entry : records_) {
        const Medication& m = entry.second;
        rows.push_back({m.id, m.name, m.drawer, std::to_string(m.quantity)});
    }
    return storage_.saveSnapshot(kFile, rows);
}

bool InventoryManager::hasMedication(const MedicationId& id) const { return records_.count(id) > 0; }

int InventoryManager::quantityOf(const MedicationId& id) const {
    const auto it = records_.find(id);
    return it == records_.end() ? 0 : it->second.quantity;
}

std::optional<DrawerId> InventoryManager::locationOf(const MedicationId& id) const {
    const auto it = records_.find(id);
    if (it == records_.end()) return std::nullopt;
    return it->second.drawer;
}

std::optional<MedicationId> InventoryManager::medicationAt(const DrawerId& drawer) const {
    for (const auto& entry : records_) {
        if (entry.second.drawer == drawer) return entry.first;
    }
    return std::nullopt;
}

std::string InventoryManager::nameOf(const MedicationId& id) const {
    const auto it = records_.find(id);
    return it == records_.end() ? id : it->second.name;
}

std::vector<Medication> InventoryManager::medications() const {
    std::vector<Medication> out;
    for (const auto& entry : records_) out.push_back(entry.second);
    return out;
}

std::map<DrawerId, int> InventoryManager::expectedCounts() const {
    std::map<DrawerId, int> out;
    for (const auto& entry : records_) out[entry.second.drawer] = entry.second.quantity;
    return out;
}

OpResult InventoryManager::canDispense(const MedicationId& id, int quantity) const {
    if (!hasMedication(id))
        return OpResult::failure(ResultCode::UnknownMedication, "Medication '" + id + "' is not stocked in this cabinet.");
    if (quantity < 1)
        return OpResult::failure(ResultCode::InvalidQuantity, "Please request a quantity of 1 or more.");
    const int stock = quantityOf(id);
    if (stock < quantity)
        return OpResult::failure(ResultCode::InsufficientStock,
                                 "Not enough " + nameOf(id) + ": " + std::to_string(stock) + " recorded, " +
                                     std::to_string(quantity) + " requested.");
    return OpResult::success("");
}

OpResult InventoryManager::change(const MedicationId& id, int delta, const Badge& by, const std::string& reason) {
    const auto it = records_.find(id);
    if (it == records_.end())
        return OpResult::failure(ResultCode::UnknownMedication, "Medication '" + id + "' is not stocked in this cabinet.");
    const int before = it->second.quantity;
    const int after = before + delta;
    if (after < 0)  // INV-INV-1
        return OpResult::failure(ResultCode::InvalidQuantity, "The recorded quantity cannot go below zero.");
    it->second.quantity = after;
    if (!save()) {  // persist first, then report
        it->second.quantity = before;  // INV-INV-2: roll back
        return OpResult::failure(ResultCode::StorageError, "The inventory change could not be saved.");
    }
    const bool removed = delta < 0;
    logger_.recordItem(EventType::INVENTORY_CHANGED, by, id, removed ? -delta : delta, removed ? "REMOVED" : "ADDED",
                       std::to_string(before) + "->" + std::to_string(after) + " reason=" + reason);
    const Medication snapshot = it->second;
    for (const ChangeListener& listener : listeners_) listener(snapshot);
    return OpResult::success(nameOf(id) + ": " + std::to_string(before) + " -> " + std::to_string(after) + ".");
}

OpResult InventoryManager::recordRemoval(const MedicationId& id, int n, const Badge& by, const std::string& reason) {
    if (n < 1) return OpResult::failure(ResultCode::InvalidQuantity, "A removal must be 1 or more units.");
    return change(id, -n, by, reason);
}

OpResult InventoryManager::recordAddition(const MedicationId& id, int n, const Badge& by, const std::string& reason) {
    if (n < 1) return OpResult::failure(ResultCode::InvalidQuantity, "An addition must be 1 or more units.");
    return change(id, n, by, reason);
}

OpResult InventoryManager::replenishAuthorized() {
    return access_.authorize(Permission::Replenish, "replenish medication");
}

void InventoryManager::addChangeListener(ChangeListener listener) { listeners_.push_back(std::move(listener)); }

}  // namespace adc
