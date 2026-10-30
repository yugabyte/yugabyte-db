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

#pragma once

#include "yb/common/entity_ids_types.h"
#include "yb/master/master_heartbeat.pb.h"

#include "yb/tserver/heartbeater.h"

namespace yb {
namespace tserver {

class TServerMetricsHeartbeatDataProvider : public PeriodicalHeartbeatDataProvider {
 public:
  explicit TServerMetricsHeartbeatDataProvider(TabletServer* server);

  void AddData(
      const master::TSHeartbeatResponsePB& last_resp, master::TSHeartbeatRequestPB* req) override;
  void OnHeartbeatResponse(const master::TSHeartbeatResponsePB& resp) override;

 private:
  void DoAddData(bool needs_full_tablet_report, master::TSHeartbeatRequestPB* req) override;
  MonoDelta Period() const override;

  uint64_t CalculateUptime();

  MonoTime start_time_;

  // Stores the total read and writes ops for computing iops.
  uint64_t prev_reads_ = 0;
  uint64_t prev_writes_ = 0;

  // Per-tablet storage metadata the master confirmed processing, and the entries in the heartbeat
  // in flight that move into acked_storage_metadata_ once its response confirms them.
  std::unordered_map<TabletId, master::TabletDriveStorageMetadataPB> acked_storage_metadata_;
  std::unordered_map<TabletId, master::TabletDriveStorageMetadataPB> pending_storage_metadata_;
};

} // namespace tserver
} // namespace yb
