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

#include "yb/cdc/xrepl_types.h"

#include "yb/common/common_fwd.h"

#include "yb/common/entity_ids_types.h"
#include "yb/util/enums.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

namespace master {

class PgAttributePB;

}  // namespace master

// Object types used to manage eXternal REPLication (XREPL) of data from a YugabyteDB.
// xCluster replicates data to another YugabyteDB, and CDC replicates the data to external
// databases or files.

namespace cdc {

static const char* const kIdType = "id_type";
static const char* const kTableId = "TABLEID";

typedef std::unordered_map<SchemaVersion, SchemaVersion> XClusterSchemaVersionMap;
typedef std::unordered_map<uint32_t, XClusterSchemaVersionMap> ColocatedSchemaVersionMap;
typedef std::unordered_map<xrepl::StreamId, XClusterSchemaVersionMap> StreamSchemaVersionMap;
typedef std::unordered_map<xrepl::StreamId, ColocatedSchemaVersionMap>
    StreamColocatedSchemaVersionMap;

constexpr uint32_t kInvalidSchemaVersion = std::numeric_limits<uint32_t>::max();


YB_STRONGLY_TYPED_BOOL(StreamModeTransactional);
YB_DEFINE_ENUM(RefreshStreamMapOption, (kNone)(kAlways)(kIfInitiatedState));

using EnumOidLabelMap = std::unordered_map<uint32_t, std::string>;
using EnumLabelCache = std::unordered_map<NamespaceName, EnumOidLabelMap>;

using CompositeAttsMap = std::unordered_map<uint32_t, std::vector<master::PgAttributePB>>;
using CompositeTypeCache = std::unordered_map<NamespaceName, CompositeAttsMap>;

static const char* const kRecordType = "record_type";
static const char* const kRecordFormat = "record_format";
static const char* const kSourceType = "source_type";
static const char* const kCheckpointType = "checkpoint_type";
static const char* const kStreamState = "state";
static const char* const kNamespaceId = "NAMESPACEID";
// NOTE: Do not add new options here. Create them as explicit PB fields.

}  // namespace cdc
}  // namespace yb
