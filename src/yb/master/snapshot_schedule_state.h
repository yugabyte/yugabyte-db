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

#ifndef YB_MASTER_SNAPSHOT_SCHEDULE_STATE_H
#define YB_MASTER_SNAPSHOT_SCHEDULE_STATE_H

#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_backup.pb.h"

namespace yb {
namespace master {

class SnapshotScheduleState {
 public:
  SnapshotScheduleState(
      SnapshotCoordinatorContext* context, const CreateSnapshotScheduleRequestPB& req);

  SnapshotScheduleState(
      SnapshotCoordinatorContext* context, const SnapshotScheduleId& id,
      const SnapshotScheduleOptionsPB& options);

  const SnapshotScheduleId& id() const {
    return id_;
  }

  bool ShouldUpdate(const SnapshotScheduleState& other) const {
    return true;
  }

  CHECKED_STATUS StoreToWriteBatch(docdb::KeyValueWriteBatchPB* write_batch);
  CHECKED_STATUS ToPB(SnapshotScheduleInfoPB* pb);
  std::string ToString() const;

 private:
  SnapshotCoordinatorContext& context_;
  SnapshotScheduleId id_;
  SnapshotScheduleOptionsPB options_;
};

} // namespace master
} // namespace yb

#endif  // YB_MASTER_SNAPSHOT_SCHEDULE_STATE_H
