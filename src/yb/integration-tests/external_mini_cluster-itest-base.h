// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <string>
#include <unordered_map>
#include <vector>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/gutil/stl_util.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/pstack_watcher.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

namespace yb {

// Simple base utility class to provide an external mini cluster with common
// setup routines useful for integration tests.
class ExternalMiniClusterITestBase : public YBTest {
 public:
  virtual void TearDown() override;

  Result<TabletId> GetSingleTabletId(const TableName& table_name);

 protected:
  virtual void SetUpOptions(ExternalMiniClusterOptions& opts);

  void StartCluster(const std::vector<std::string>& extra_ts_flags = std::vector<std::string>(),
                    const std::vector<std::string>& extra_master_flags = std::vector<std::string>(),
                    int num_tablet_servers = 3,
                    int num_masters = 1,
                    bool enable_ysql = false);

  Status StartCluster(ExternalMiniClusterOptions opts);

  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::unique_ptr<itest::ExternalMiniClusterFsInspector> inspect_;
  std::unique_ptr<client::YBClient> client_;
  itest::TabletServerMap ts_map_;
};

}  // namespace yb
