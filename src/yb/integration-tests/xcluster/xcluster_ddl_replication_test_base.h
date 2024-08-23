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

#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

namespace yb {

class XClusterDDLReplicationTestBase : public XClusterYsqlTestBase {
 public:
  struct SetupParams {
    // By default start with no consumer or producer tables.
    std::vector<uint32_t> num_consumer_tablets = {};
    std::vector<uint32_t> num_producer_tablets = {};
    // We only create one pg proxy per cluster, so we need to ensure that the target ddl_queue table
    // leader is on the that tserver (so that setting xcluster context works properly).
    uint32_t replication_factor = 1;
    uint32_t num_masters = 1;
    bool ranged_partitioned = false;
  };

  XClusterDDLReplicationTestBase() = default;
  ~XClusterDDLReplicationTestBase() = default;

  virtual void SetUp() override;

  Status SetUpClusters();

  Status SetUpClusters(const SetupParams& params);

  Status EnableDDLReplicationExtension();

  Result<std::shared_ptr<client::YBTable>> GetProducerTable(
      const client::YBTableName& producer_table_name);

  Result<std::shared_ptr<client::YBTable>> GetConsumerTable(
      const client::YBTableName& producer_table_name);

  void InsertRowsIntoProducerTableAndVerifyConsumer(const client::YBTableName& producer_table_name);

  Status PrintDDLQueue(Cluster& cluster);
};

}  // namespace yb
