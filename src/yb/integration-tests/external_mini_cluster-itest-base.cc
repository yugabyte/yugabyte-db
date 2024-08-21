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

#include "yb/integration-tests/external_mini_cluster-itest-base.h"

namespace yb {

void ExternalMiniClusterITestBase::SetUpOptions(ExternalMiniClusterOptions& opts) {
  // Fsync causes flakiness on EC2.
  opts.extra_tserver_flags.push_back("--never_fsync");
}

void ExternalMiniClusterITestBase::StartCluster(
    const std::vector<std::string>& extra_ts_flags,
    const std::vector<std::string>& extra_master_flags, int num_tablet_servers, int num_masters,
    bool enable_ysql) {
  ExternalMiniClusterOptions opts;
  opts.num_masters = num_masters;
  opts.num_tablet_servers = num_tablet_servers;
  opts.extra_master_flags = extra_master_flags;
  opts.extra_tserver_flags = extra_ts_flags;
  opts.enable_ysql = enable_ysql;

  ASSERT_OK(StartCluster(opts));
}

Status ExternalMiniClusterITestBase::StartCluster(ExternalMiniClusterOptions opts) {
  SetUpOptions(opts);

  cluster_.reset(new ExternalMiniCluster(opts));
  RETURN_NOT_OK(cluster_->Start());
  inspect_.reset(new itest::ExternalMiniClusterFsInspector(cluster_.get()));
  ts_map_ = VERIFY_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  client_ = VERIFY_RESULT(cluster_->CreateClient());

  return Status::OK();
}

void ExternalMiniClusterITestBase::TearDown() {
  client_.reset();
  if (cluster_) {
    if (HasFatalFailure()) {
      LOG(INFO) << "Found fatal failure";
      for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
        if (!cluster_->tablet_server(i)->IsProcessAlive()) {
          LOG(INFO) << "Tablet server " << i << " is not running. Cannot dump its stacks.";
          continue;
        }
        LOG(INFO) << "Attempting to dump stacks of TS " << i << " with UUID "
                  << cluster_->tablet_server(i)->uuid() << " and pid "
                  << cluster_->tablet_server(i)->pid();
        WARN_NOT_OK(
            PstackWatcher::DumpPidStacks(cluster_->tablet_server(i)->pid()),
            "Couldn't dump stacks");
      }
    }
    cluster_->Shutdown();
  }
  YBTest::TearDown();
  ts_map_.clear();
}

Result<TabletId> ExternalMiniClusterITestBase::GetSingleTabletId(const TableName& table_name) {
  TabletId tablet_id_to_split;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    const auto ts = cluster_->tablet_server(i);
    const auto tablets = VERIFY_RESULT(cluster_->GetTablets(ts));
    for (const auto& tablet : tablets) {
      if (tablet.table_name() == table_name) {
        return tablet.tablet_id();
      }
    }
  }
  return STATUS(NotFound, Format("No tablet found for table $0.", table_name));
}

}  // namespace yb
