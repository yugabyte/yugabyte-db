// Copyright (c) YugaByte, Inc.
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

#include "yb/master/snapshot_schedule_state.h"

#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/key_bytes.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/snapshot_coordinator_context.h"

#include "yb/util/pb_util.h"

namespace yb {
namespace master {

SnapshotScheduleState::SnapshotScheduleState(
    SnapshotCoordinatorContext* context, const CreateSnapshotScheduleRequestPB &req)
    : context_(*context), id_(SnapshotScheduleId::GenerateRandom()), options_(req.options()) {
}

SnapshotScheduleState::SnapshotScheduleState(
    SnapshotCoordinatorContext* context, const SnapshotScheduleId& id,
    const SnapshotScheduleOptionsPB& options)
    : context_(*context), id_(id), options_(options) {
}

Status SnapshotScheduleState::StoreToWriteBatch(docdb::KeyValueWriteBatchPB* out) {
  auto encoded_key = VERIFY_RESULT(EncodedKey(
      SysRowEntry::SNAPSHOT_SCHEDULE, id_.AsSlice(), &context_));
  auto pair = out->add_write_pairs();
  pair->set_key(encoded_key.AsSlice().cdata(), encoded_key.size());
  faststring value;
  value.push_back(docdb::ValueTypeAsChar::kString);
  pb_util::AppendToString(options_, &value);
  pair->set_value(value.data(), value.size());
  return Status::OK();
}

Status SnapshotScheduleState::ToPB(SnapshotScheduleInfoPB* pb) const {
  pb->set_id(id_.data(), id_.size());
  *pb->mutable_options() = options_;
  return Status::OK();
}

std::string SnapshotScheduleState::ToString() const {
  return YB_CLASS_TO_STRING(id, options);
}

void SnapshotScheduleState::PrepareOperations(
    HybridTime last_snapshot_time, HybridTime now, SnapshotScheduleOperations* operations) {
  if (creating_snapshot_id_ ||
      (last_snapshot_time && last_snapshot_time.AddSeconds(options_.interval_sec()) > now)) {
    return;
  }
  creating_snapshot_id_ = TxnSnapshotId::GenerateRandom();
  operations->push_back(SnapshotScheduleOperation {
    .schedule_id = id_,
    .filter = options_.filter(),
    .snapshot_id = creating_snapshot_id_,
  });
}

void SnapshotScheduleState::SnapshotFinished(
    const TxnSnapshotId& snapshot_id, const Status& status) {
  if (creating_snapshot_id_ != snapshot_id) {
    return;
  }
  creating_snapshot_id_ = TxnSnapshotId::Nil();
}

} // namespace master
} // namespace yb
