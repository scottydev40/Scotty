#include "file_share_state.h"
#include "app_paths.h"
#include "status_mapper.h"

#include <QDateTime>

FileShareState::FileShareState() : log_path_(DefaultLogPath()) {}

void FileShareState::AddOrUpdateTarget(qlonglong id, const QString& name,
                                       bool is_incoming, int device_type) {
  // Preserve a previously-known device type when the caller passes -1.
  auto resolveType = [&](int row_index) -> int {
    if (device_type >= 0) return device_type;
    if (row_index >= 0 && row_index < discovered_targets_.size())
      return discovered_targets_[row_index].toMap()
          .value(QStringLiteral("deviceType"), 0).toInt();
    return 0;
  };

  // Same id already known → update in place.
  if (discovered_row_by_target_.contains(id)) {
    target_names_[id] = name;
    const int row_index = discovered_row_by_target_.value(id);
    if (row_index >= 0 && row_index < discovered_targets_.size()) {
      QVariantMap target;
      target[QStringLiteral("id")] = id;
      target[QStringLiteral("name")] = name;
      target[QStringLiteral("isIncoming")] = is_incoming;
      target[QStringLiteral("deviceType")] = resolveType(row_index);
      discovered_targets_[row_index] = target;
    }
    return;
  }

  // New id. Nearby rotates endpoint ids, so the same physical device can
  // reappear under a fresh id after a transfer. If another id already
  // represents this name, reuse its row (and migrate any transfer) instead of
  // adding a duplicate card.
  for (auto it = discovered_row_by_target_.begin();
       it != discovered_row_by_target_.end(); ++it) {
    const qlonglong old_id = it.key();
    if (target_names_.value(old_id) != name) {
      continue;
    }
    const int row_index = it.value();

    // Migrate an existing transfer keyed by old_id → id so the "Sent" card
    // follows the device to its new endpoint id.
    if (transfer_row_by_target_.contains(old_id)) {
      const int tr = transfer_row_by_target_.take(old_id);
      if (tr >= 0 && tr < transfers_.size()) {
        QVariantMap transfer = transfers_[tr].toMap();
        transfer[QStringLiteral("targetId")] = id;
        transfers_[tr] = transfer;
        transfer_row_by_target_.insert(id, tr);
      }
    }

    discovered_row_by_target_.erase(it);
    discovered_row_by_target_.insert(id, row_index);
    target_names_.remove(old_id);
    target_names_[id] = name;
    if (row_index >= 0 && row_index < discovered_targets_.size()) {
      QVariantMap target;
      target[QStringLiteral("id")] = id;
      target[QStringLiteral("name")] = name;
      target[QStringLiteral("isIncoming")] = is_incoming;
      target[QStringLiteral("deviceType")] = resolveType(row_index);
      discovered_targets_[row_index] = target;
    }
    return;
  }

  // Genuinely new device.
  target_names_[id] = name;
  QVariantMap target;
  target[QStringLiteral("id")] = id;
  target[QStringLiteral("name")] = name;
  target[QStringLiteral("isIncoming")] = is_incoming;
  target[QStringLiteral("deviceType")] = (device_type >= 0 ? device_type : 0);
  discovered_row_by_target_[id] = discovered_targets_.size();
  discovered_targets_.append(target);
}

void FileShareState::RemoveTarget(qlonglong id) {
  if (HasActiveTransferForTarget(id)) {
    AddPendingTargetRemoval(id);
    return;
  }

  RemovePendingTargetRemoval(id);
  target_names_.remove(id);

  if (!discovered_row_by_target_.contains(id)) {
    return;
  }

  const int removed_index = discovered_row_by_target_.take(id);
  if (removed_index < 0 || removed_index >= discovered_targets_.size()) {
    return;
  }

  discovered_targets_.removeAt(removed_index);
  for (auto it = discovered_row_by_target_.begin();
       it != discovered_row_by_target_.end(); ++it) {
    if (it.value() > removed_index) {
      it.value() = it.value() - 1;
    }
  }
}

void FileShareState::ClearTargets() {
  // Snapshot the ids first — RemoveTarget mutates discovered_row_by_target_ as
  // it reindexes, so iterating it directly would be unsafe.
  const QList<qlonglong> ids = discovered_row_by_target_.keys();
  for (qlonglong id : ids) {
    RemoveTarget(id);
  }
}

QString FileShareState::GetTargetName(qlonglong id) const {
  const QString name = target_names_.value(id).trimmed();
  return name.isEmpty() ? QStringLiteral("Unknown device") : name;
}

bool FileShareState::HasTarget(qlonglong id) const {
  return discovered_row_by_target_.contains(id);
}

void FileShareState::AddOrUpdateTransfer(
    qlonglong target_id, const QString& target_name, const QString& status,
    double progress, qulonglong transferred_bytes, const QString& direction,
    const QString& file_name, const QString& file_path,
    double speed_bytes_per_sec, int current_file, int total_files) {
  const bool now_active = StatusMapper::IsActiveTransferStatus(status);
  const qlonglong now_ms = QDateTime::currentMSecsSinceEpoch();

  // If a fresh session starts on a device whose previous row already finished,
  // archive that finished row (drop its target mapping and flag it) so the new
  // one stacks below it rather than overwriting the "done" state. The archived
  // row keeps its endedAt and is swept once its TTL passes.
  if (now_active && transfer_row_by_target_.contains(target_id)) {
    const int prev_index = transfer_row_by_target_.value(target_id);
    if (prev_index >= 0 && prev_index < transfers_.size()) {
      QVariantMap prev = transfers_[prev_index].toMap();
      if (!StatusMapper::IsActiveTransferStatus(
              prev.value(QStringLiteral("status")).toString())) {
        prev[QStringLiteral("archived")] = true;
        transfers_[prev_index] = prev;
        transfer_row_by_target_.remove(target_id);
      }
    }
  }

  QVariantMap transfer{
      {QStringLiteral("targetId"), target_id},
      {QStringLiteral("targetName"), target_name},
      {QStringLiteral("status"), status},
      {QStringLiteral("progress"), progress},
      {QStringLiteral("transferredBytes"), transferred_bytes},
      {QStringLiteral("direction"), direction},
      {QStringLiteral("fileName"), file_name},
      {QStringLiteral("filePath"), file_path},
      {QStringLiteral("speed"), speed_bytes_per_sec},
      {QStringLiteral("currentFile"), current_file},
      {QStringLiteral("totalFiles"), total_files},
      {QStringLiteral("archived"), false},
      // Stamped when the row is (or becomes) finished; 0 while active. Drives
      // the expiry sweep so completed rows fade after a few seconds.
      {QStringLiteral("endedAt"), now_active ? qlonglong(0) : now_ms},
  };

  if (transfer_row_by_target_.contains(target_id)) {
    const int row_index = transfer_row_by_target_.value(target_id);
    if (row_index >= 0 && row_index < transfers_.size()) {
      // Preserve the original finish time across repeated terminal updates so
      // the TTL is measured from when it first ended, not the latest refresh.
      if (!now_active) {
        const qlonglong prev_ended =
            transfers_[row_index].toMap().value(QStringLiteral("endedAt"))
                .toLongLong();
        if (prev_ended > 0)
          transfer[QStringLiteral("endedAt")] = prev_ended;
      }
      transfers_[row_index] = transfer;
      return;
    }
  }

  transfer_row_by_target_.insert(target_id, transfers_.size());
  transfers_.append(transfer);
}

bool FileShareState::SweepExpiredTransfers(qlonglong now_ms, qlonglong ttl_ms) {
  bool changed = false;
  QVariantList kept;
  QHash<qlonglong, int> new_map;
  for (const QVariant& row_value : transfers_) {
    const QVariantMap row = row_value.toMap();
    const QString status = row.value(QStringLiteral("status")).toString();
    const bool active = StatusMapper::IsActiveTransferStatus(status);
    const qlonglong ended = row.value(QStringLiteral("endedAt")).toLongLong();
    if (!active && ended > 0 && (now_ms - ended) >= ttl_ms) {
      changed = true;  // expired — drop it
      continue;
    }
    // Only a non-archived row owns its target's update slot.
    if (!row.value(QStringLiteral("archived")).toBool()) {
      new_map.insert(row.value(QStringLiteral("targetId")).toLongLong(),
                     kept.size());
    }
    kept.append(row_value);
  }
  if (changed) {
    transfers_ = kept;
    transfer_row_by_target_ = new_map;
  }
  return changed;
}

void FileShareState::RemoveTransfer(qlonglong target_id) {
  if (!transfer_row_by_target_.contains(target_id)) {
    return;
  }

  const int removed_index = transfer_row_by_target_.take(target_id);
  if (removed_index < 0 || removed_index >= transfers_.size()) {
    return;
  }

  transfers_.removeAt(removed_index);
  for (auto it = transfer_row_by_target_.begin();
       it != transfer_row_by_target_.end(); ++it) {
    if (it.value() > removed_index) {
      it.value() = it.value() - 1;
    }
  }
}

bool FileShareState::HasActiveTransferForTarget(qlonglong target_id) const {
  for (const QVariant& row_value : transfers_) {
    const QVariantMap row = row_value.toMap();
    if (row.value(QStringLiteral("targetId")).toLongLong() != target_id) {
      continue;
    }

    const QString status = row.value(QStringLiteral("status")).toString();
    if (StatusMapper::IsActiveTransferStatus(status)) {
      return true;
    }
  }
  return false;
}

bool FileShareState::HasActiveTransfers() const {
  for (const QVariant& row_value : transfers_) {
    const QVariantMap row = row_value.toMap();
    const QString status = row.value(QStringLiteral("status")).toString();
    if (StatusMapper::IsActiveTransferStatus(status)) {
      return true;
    }
  }
  return false;
}

void FileShareState::ClearFinishedTransfers() {
  QVariantList kept;
  transfer_row_by_target_.clear();
  for (const QVariant& row_value : transfers_) {
    const QVariantMap row = row_value.toMap();
    const QString status = row.value(QStringLiteral("status")).toString();
    if (StatusMapper::IsActiveTransferStatus(status)) {
      transfer_row_by_target_.insert(
          row.value(QStringLiteral("targetId")).toLongLong(), kept.size());
      kept.append(row_value);
    }
  }
  transfers_ = kept;
}

void FileShareState::AddPendingTargetRemoval(qlonglong id) {
  pending_target_removals_.insert(id);
}

void FileShareState::RemovePendingTargetRemoval(qlonglong id) {
  pending_target_removals_.remove(id);
}

bool FileShareState::IsPendingTargetRemoval(qlonglong id) const {
  return pending_target_removals_.contains(id);
}

void FileShareState::ClearAll() {
  discovered_targets_.clear();
  discovered_row_by_target_.clear();
  target_names_.clear();
  transfers_.clear();
  transfer_row_by_target_.clear();
  pending_target_removals_.clear();
}
