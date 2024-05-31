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

#include "yb/util/pb_util.h"
#include "yb/util/strongly_typed_uuid.h"

#pragma once

namespace yb {

YB_STRONGLY_TYPED_UUID_DECL(SnapshotScheduleId);

namespace master {

struct TabletDeleteRetainerInfo {
  std::vector<SnapshotScheduleId> snapshot_schedules;
  bool remove_from_name_map = false;
  bool active_snapshot = false;
  bool active_xcluster = false;
  bool active_cdcsdk = false;

  static const TabletDeleteRetainerInfo& AlwaysDelete() {
    static const TabletDeleteRetainerInfo kAlwaysDelete;
    return kAlwaysDelete;
  }

  bool IsHideOnly() const;

  std::string ToString() const;

  RepeatedBytes RetainedBySnapshotSchedules() const;
};

}  // namespace master
}  // namespace yb
