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

#include <string>
#include <unordered_map>

#include "yb/common/common_fwd.h"
#include "yb/common/common_types.pb.h"

#include "yb/common/entity_ids_types.h"
#include "yb/util/enums.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/strongly_typed_uuid.h"

namespace yb {

// Object types used to manage eXternal REPLication (XREPL) of data from a YugabyteDB.
// xCluster replicates data to another YugabyteDB, and CDC replicates the data to external
// databases or files.
namespace xrepl {
YB_STRONGLY_TYPED_UUID_DECL(StreamId);
}

namespace cdc {
static const char* const kIdType = "id_type";
static const char* const kTableId = "TABLEID";

YB_STRONGLY_TYPED_STRING(ReplicationGroupId);

// Maps a tablet id -> stream id -> replication error -> error detail.
typedef std::unordered_map<ReplicationErrorPb, std::string> ReplicationErrorMap;
typedef std::unordered_map<xrepl::StreamId, ReplicationErrorMap> StreamReplicationErrorMap;
typedef std::unordered_map<TabletId, StreamReplicationErrorMap> TabletReplicationErrorMap;

typedef std::unordered_map<SchemaVersion, SchemaVersion> XClusterSchemaVersionMap;
typedef std::unordered_map<uint32_t, XClusterSchemaVersionMap> ColocatedSchemaVersionMap;
typedef std::unordered_map<xrepl::StreamId, XClusterSchemaVersionMap> StreamSchemaVersionMap;
typedef std::unordered_map<xrepl::StreamId, ColocatedSchemaVersionMap>
    StreamColocatedSchemaVersionMap;

constexpr uint32_t kInvalidSchemaVersion = std::numeric_limits<uint32_t>::max();


YB_STRONGLY_TYPED_BOOL(StreamModeTransactional);
YB_DEFINE_ENUM(RefreshStreamMapOption, (kNone)(kAlways)(kIfInitiatedState));
}  // namespace cdc
}  // namespace yb
