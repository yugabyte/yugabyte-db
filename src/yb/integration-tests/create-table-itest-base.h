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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/client/table.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/common_types.pb.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
class PlacementInfoPB;
class ReplicationInfoPB;
}  // namespace yb

using strings::Substitute;
using yb::client::YBTableName;
using yb::client::YBTableType;

namespace yb {

static const YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "test-table");

class CreateTableITestBase : public ExternalMiniClusterITestBase {
 public:
  Status CreateTableWithPlacement(
      const ReplicationInfoPB& replication_info, const std::string& table_suffix,
      const YBTableType table_type = YBTableType::YQL_TABLE_TYPE);

  Result<bool> VerifyTServerTablets(
      int idx, int num_tablets, int num_leaders, const std::string& table_name,
      bool verify_leaders);

  void PreparePlacementInfo(
      const std::unordered_map<std::string, int>& zone_to_replica_count, int num_replicas,
      PlacementInfoPB* placement_info);

  void AddTServerInZone(const std::string& zone);
};

}  // namespace yb
