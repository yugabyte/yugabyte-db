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

#pragma once

#include "yb/master/master_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace master {

class TabletSplitCandidateFilterIf {
 public:
  virtual ~TabletSplitCandidateFilterIf() {}

  // Table-level checks for whether we can split tablets in this table.
  virtual Status XReplValidateSplitCandidateTable(const TableId& table_id) const = 0;
  virtual Result<bool> IsTablePartOfSomeSnapshotSchedule(const TableInfo& table_info) = 0;

  // Returns Status::OK if we should split a tablet based on the provided drive_info, and a status
  // explaining why not otherwise.
  virtual Status ShouldSplitValidCandidate(
      const TabletInfo& tablet_info, const TabletReplicaDriveInfo& drive_info) const = 0;

  virtual Result<ReplicationInfoPB> GetTableReplicationInfo(const TableInfoPtr& table) = 0;

  virtual Status CanAddPartitionsToTable(
    size_t desired_partitions, const PlacementInfoPB& placement_info) = 0;

  virtual Status CanSupportAdditionalTablet(
      const TableInfoPtr& table, const ReplicationInfoPB& replication_info) const = 0;

  virtual void IncrementSplitBlockedByTabletLimitCounter() = 0;
};

}  // namespace master
}  // namespace yb
