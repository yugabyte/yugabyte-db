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

#include <map>
#include <memory>
#include <set>
#include <string>

#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/client/client_fwd.h"
#include "yb/client/client-test-util.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"

#include "yb/common/common.pb.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/metrics.h"
#include "yb/util/path_util.h"
#include "yb/util/string_util.h"
#include "yb/util/tsan_util.h"

using strings::Substitute;
using yb::client::YBTableName;
using yb::client::YBTableType;

namespace yb {

static const YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "test-table");

class CreateTableITestBase : public ExternalMiniClusterITestBase {
 public:
  Status CreateTableWithPlacement(
      const master::ReplicationInfoPB& replication_info, const std::string& table_suffix,
      const YBTableType table_type = YBTableType::YQL_TABLE_TYPE);

  Result<bool> VerifyTServerTablets(
      int idx, int num_tablets, int num_leaders, const std::string& table_name,
      bool verify_leaders);

  void PreparePlacementInfo(
      const std::unordered_map<std::string, int>& zone_to_replica_count, int num_replicas,
      master::PlacementInfoPB* placement_info);

  void AddTServerInZone(const std::string& zone);
};

}  // namespace yb
