// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/master_host/sys_catalog_tap.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"

#include "master/server/src/ffi.rs.h"

namespace yb::master_host {

rust::Vec<uint8_t> ToRustBytes(Slice bytes) {
  return CopyBytes(rust::Slice<const uint8_t>(bytes.data(), bytes.size()));
}

SysCatalogTap::SysCatalogTap(tablet::SnapshotCoordinator* inner, ColumnId metadata_column)
    : inner_(*inner), metadata_column_(metadata_column) {}

SysCatalogTap::~SysCatalogTap() = default;

void SysCatalogTap::SetSink(rust::Box<ApplySink> sink) {
  std::lock_guard lock(sink_mutex_);
  sink_.emplace(std::move(sink));
}

void SysCatalogTap::PostReload(const std::string& reason, HybridTime min_time) {
  LOG(INFO) << "Rust catalog reload: " << reason;
  std::lock_guard lock(sink_mutex_);
  if (sink_) {
    (*sink_)->OnReload(rust::String::lossy(reason), min_time.ToUint64());
  }
}

void SysCatalogTap::PostLeadershipChange() {
  std::lock_guard lock(sink_mutex_);
  if (sink_) {
    (*sink_)->OnLeadershipChange();
  }
}

Status SysCatalogTap::CreateReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation) {
  return inner_.CreateReplicated(leader_term, operation);
}

Status SysCatalogTap::DeleteReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation) {
  return inner_.DeleteReplicated(leader_term, operation);
}

Status SysCatalogTap::RestoreSysCatalogReplicated(
    int64_t leader_term, const tablet::SnapshotOperation& operation, Status* complete_status) {
  return inner_.RestoreSysCatalogReplicated(leader_term, operation, complete_status);
}

Status SysCatalogTap::Load(tablet::Tablet* tablet) {
  return inner_.Load(tablet);
}

Status SysCatalogTap::ApplyWritePair(const Slice& key, const Slice& value) {
  auto status = Decode(key, value);
  if (!status.ok()) {
    // A row Rust cannot see would leave its catalog silently wrong; a reload rescans the tablet.
    LOG(DFATAL) << "Undecodable sys catalog write pair: " << status;
    pending_failed_ = true;
  }
  return inner_.ApplyWritePair(key, value);
}

// Keys are (entry_type int32, entry_id string) with the metadata column as the only value column;
// values are a string or a tombstone. A delete is a row tombstone (no subkey).
Status SysCatalogTap::Decode(const Slice& key, const Slice& value) {
  dockv::SubDocKey sub_doc_key;
  RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));
  const auto& doc_key = sub_doc_key.doc_key();
  if (doc_key.has_cotable_id()) {
    // YSQL catalog tables.
    return Status::OK();
  }
  const auto& range = doc_key.range_group();
  SCHECK_EQ(range.size(), 2U, Corruption, "Unexpected sys catalog key " + sub_doc_key.ToString());
  SCHECK(range[0].IsInt32() && range[1].IsString(), Corruption,
         "Unexpected sys catalog key " + sub_doc_key.ToString());

  const auto& subkeys = sub_doc_key.subkeys();
  const bool is_row = subkeys.empty();
  if (!is_row && !(subkeys.size() == 1 && subkeys[0].IsColumnId() &&
                   subkeys[0].GetColumnId() == metadata_column_)) {
    // The liveness column of an INSERT.
    return Status::OK();
  }

  if (is_row && dockv::GetPackedRowVersion(value)) {
    // An INSERT through WriteSysCatalogEntry in emergency repair mode, with packed rows on (the
    // debug-build default). Rare enough to rescan rather than decode.
    pending_reload_ = true;
    return Status::OK();
  }
  dockv::Value decoded;
  RETURN_NOT_OK(decoded.Decode(value));
  const auto type = decoded.primitive_value().value_type();
  if (type == dockv::ValueEntryType::kTombstone) {
    pending_.emplace_back(range[0].GetInt32(), range[1].GetString(), std::string(), true);
    return Status::OK();
  }
  SCHECK(!is_row, Corruption,
         Format("Unexpected row-level value of type $0 for $1", type, sub_doc_key.ToString()));
  SCHECK_EQ(type, dockv::ValueEntryType::kString, Corruption,
            "Unexpected value type for " + sub_doc_key.ToString());
  pending_.emplace_back(
      range[0].GetInt32(), range[1].GetString(), decoded.primitive_value().GetString(), false);
  return Status::OK();
}

void SysCatalogTap::RestoredOnTablet(const tablet::SnapshotOperation& operation) {
  inner_.RestoredOnTablet(operation);
  PostReload("sys catalog restored on tablet", operation.hybrid_time());
}

void SysCatalogTap::AppliedWriteBatch(const OpId& op_id, HybridTime write_time) {
  inner_.AppliedWriteBatch(op_id, write_time);
  const bool failed = std::exchange(pending_failed_, false);
  const bool reload = std::exchange(pending_reload_, false) || failed;
  if (pending_.empty() && !reload) {
    return;
  }
  rust::Vec<RowChangeFfi> rows;
  rows.reserve(pending_.size());
  for (auto& [entry_type, id, data, deleted] : pending_) {
    rows.push_back(RowChangeFfi{
        .entry_type = entry_type, .id = ToRustBytes(id), .data = ToRustBytes(data),
        .deleted = deleted});
  }
  pending_.clear();
  std::lock_guard lock(sink_mutex_);
  if (!sink_) {
    return;
  }
  (*sink_)->OnAppliedBatch(op_id.term, op_id.index, write_time.ToUint64(), std::move(rows));
  if (reload) {
    (*sink_)->OnReload(
        failed ? "undecodable sys catalog write" : "packed sys catalog row",
        write_time.ToUint64());
  }
}

}  // namespace yb::master_host
