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

#include <cstdint>
#include <unordered_map>

#include "yb/client/client_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/master/master_client.fwd.h"
#include "yb/master/master_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace master {

class MiniMaster;

Status WaitForRunningTabletCount(MiniMaster* mini_master,
                                 const client::YBTableName& table_name,
                                 int expected_count,
                                 GetTableLocationsResponsePB* resp);

// What the replica load counters must agree with: every replica of every live tablet.
std::unordered_map<TabletServerId, int64_t> ExpectedReplicaLoad(MiniMaster* mini_master);

bool ReplicaLoadMatchesReplicaMaps(MiniMaster* mini_master);


} // namespace master
} // namespace yb
