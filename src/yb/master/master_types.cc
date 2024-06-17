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

#include <google/protobuf/repeated_field.h>

#include "yb/master/master_types.h"
#include "yb/util/format.h"
#include "yb/util/tostring.h"

namespace yb::master {

bool TabletDeleteRetainerInfo::IsHideOnly() const {
  return !snapshot_schedules.empty() || active_snapshot || active_xcluster || active_cdcsdk;
}

std::string TabletDeleteRetainerInfo::ToString() const {
  return Format(
      "Retained by: $0",
      YB_STRUCT_TO_STRING(snapshot_schedules, active_snapshot, active_xcluster, active_cdcsdk));
}

RepeatedBytes TabletDeleteRetainerInfo::RetainedBySnapshotSchedules() const {
  RepeatedBytes result;
  for (const auto& schedule_id : snapshot_schedules) {
    result.Add()->assign(schedule_id.AsSlice().cdata(), schedule_id.size());
  }
  return result;
}
}  // namespace yb::master
