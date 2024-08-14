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
//

#include "yb/master/clone/clone_state_entity.h"

#include <optional>

#include "yb/gutil/macros.h"
#include "yb/gutil/map-util.h"
#include "yb/master/catalog_entity_info.pb.h"

namespace yb::master {

void CloneStateInfo::Load(const SysCloneStatePB& metadata) {
  MetadataCowWrapper<PersistentCloneStateInfo>::Load(metadata);
}

CloneStateInfo::CloneStateInfo(std::string id):
    clone_request_id_(std::move(id)) {}

std::vector<CloneStateInfo::TabletData> CloneStateInfo::GetTabletData() {
  std::lock_guard l(mutex_);
  return tablet_data_;
}

void CloneStateInfo::AddTabletData(TabletData tablet_data) {
  std::lock_guard l(mutex_);
  tablet_data_.push_back(std::move(tablet_data));
}

LeaderEpoch CloneStateInfo::Epoch() {
  std::lock_guard l(mutex_);
  return epoch_;
}

void CloneStateInfo::SetEpoch(const LeaderEpoch& epoch) {
  std::lock_guard l(mutex_);
  epoch_ = epoch;
}

YQLDatabase CloneStateInfo::DatabaseType() {
  std::lock_guard l(mutex_);
  return database_type_;
}

void CloneStateInfo::SetDatabaseType(YQLDatabase database_type) {
  std::lock_guard l(mutex_);
  database_type_ = database_type;
}

const TxnSnapshotId& CloneStateInfo::SourceSnapshotId() {
  std::lock_guard l(mutex_);
  return source_snapshot_id_;
}

void CloneStateInfo::SetSourceSnapshotId(const TxnSnapshotId& source_snapshot_id) {
  std::lock_guard l(mutex_);
  source_snapshot_id_ = source_snapshot_id;
}

const TxnSnapshotId& CloneStateInfo::TargetSnapshotId() {
  std::lock_guard l(mutex_);
  return target_snapshot_id_;
}

void CloneStateInfo::SetTargetSnapshotId(const TxnSnapshotId& target_snapshot_id) {
  std::lock_guard l(mutex_);
  target_snapshot_id_ = target_snapshot_id;
}

const TxnSnapshotRestorationId& CloneStateInfo::RestorationId() {
  std::lock_guard l(mutex_);
  return restoration_id_;
}

void CloneStateInfo::SetRestorationId(const TxnSnapshotRestorationId& restoration_id) {
  std::lock_guard l(mutex_);
  restoration_id_ = restoration_id;
}

}  // namespace yb::master
