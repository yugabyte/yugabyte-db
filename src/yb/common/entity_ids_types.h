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

#include <string>
#include <unordered_set>

#include "yb/util/strongly_typed_string.h"

namespace yb {

// TODO: switch many of these to opaque types for additional type safety and efficiency.
using NamespaceName = std::string;
using TableName = std::string;
using UDTypeName = std::string;
using RoleName = std::string;

using NamespaceId = std::string;
using ObjectId = std::string;
using TableId = std::string;
using UDTypeId = std::string;

using PeerId = std::string;
using SnapshotId = std::string;
using TabletServerId = PeerId;
using TabletId = std::string;
using TablegroupId = std::string;
using TablespaceId = std::string;

YB_STRONGLY_TYPED_STRING(KvStoreId);
YB_STRONGLY_TYPED_STRING(ReplicationSlotName);

// TODO(#79): switch to YB_STRONGLY_TYPED_STRING
using RaftGroupId = std::string;

using NamespaceIdTableNamePair = std::pair<NamespaceId, TableName>;

using FlushRequestId = std::string;

using RedisConfigKey = std::string;

using TableIdSet = std::unordered_set<TableId>;

}  // namespace yb
