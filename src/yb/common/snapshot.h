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

#include <unordered_map>

#include <boost/functional/hash/hash.hpp>
#include <boost/optional/optional.hpp>

#include "yb/common/hybrid_time.h"

#include "yb/util/strongly_typed_uuid.h"

namespace yb {

YB_STRONGLY_TYPED_UUID_DECL(TxnSnapshotId);
YB_STRONGLY_TYPED_UUID_DECL(TxnSnapshotRestorationId);
YB_STRONGLY_TYPED_UUID_DECL(SnapshotScheduleId);

using SnapshotSchedulesToObjectIdsMap =
    std::unordered_map<SnapshotScheduleId, std::vector<std::string>, SnapshotScheduleIdHash>;

using RestorationCompleteTimeMap = std::unordered_map<
    TxnSnapshotRestorationId, HybridTime, TxnSnapshotRestorationIdHash>;

using ScheduleMinRestoreTime =
    std::unordered_map<SnapshotScheduleId, HybridTime, SnapshotScheduleIdHash>;
} // namespace yb
