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

// The apply tap: every sys-catalog row the tablet applies, handed to Rust in log order, one batch
// per Raft op. It sits between the tablet and the snapshot coordinator, which the tablet already
// calls for every applied write pair, and forwards every call to the coordinator unchanged.
//
// Apply must never block, so a batch is pushed onto Rust's unbounded queue and nothing waits for
// Rust. Changes the tap cannot see as rows (a PITR restore, a remote bootstrap, shell mode) are
// posted as a Reload instead; see src/yb/rust/master/docs/sys-catalog.md.

#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "rust/cxx.h"

#include "yb/common/column_id.h"
#include "yb/common/hybrid_time.h"

#include "yb/tablet/snapshot_coordinator.h"

#include "yb/util/slice.h"

namespace yb::master_host {

// Defined in Rust (master/server/src/ffi.rs).
struct ApplySink;

rust::Vec<uint8_t> ToRustBytes(Slice bytes);

class SysCatalogTap : public tablet::SnapshotCoordinator {
 public:
  SysCatalogTap(tablet::SnapshotCoordinator* inner, ColumnId metadata_column);
  ~SysCatalogTap();

  // Must be set before the sys-catalog tablet opens; rows applied before that are covered by the
  // Rust side's initial scan.
  void SetSink(rust::Box<ApplySink> sink);
  // `min_time`: Rust scans again at a safe time no earlier than this.
  void PostReload(const std::string& reason, HybridTime min_time);
  void PostLeadershipChange();

  Status CreateReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) override;
  Status DeleteReplicated(int64_t leader_term, const tablet::SnapshotOperation& operation) override;
  Status RestoreSysCatalogReplicated(
      int64_t leader_term, const tablet::SnapshotOperation& operation,
      Status* complete_status) override;
  Status Load(tablet::Tablet* tablet) override;
  Status ApplyWritePair(const Slice& key, const Slice& value) override;
  void AppliedWriteBatch(const OpId& op_id, HybridTime write_time) override;

 private:
  // (entry_type, entry_id, metadata, deleted).
  using PendingRow = std::tuple<int32_t, std::string, std::string, bool>;

  Status Decode(const Slice& key, const Slice& value);

  tablet::SnapshotCoordinator& inner_;
  const ColumnId metadata_column_;
  std::mutex sink_mutex_;
  std::optional<rust::Box<ApplySink>> sink_;
  // Rows of the batch being applied. The tablet applies one op at a time, so only the apply thread
  // touches it.
  std::vector<PendingRow> pending_;
  bool pending_failed_ = false;
  bool pending_reload_ = false;
};

}  // namespace yb::master_host
