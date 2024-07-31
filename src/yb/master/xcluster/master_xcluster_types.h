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

#include "yb/cdc/xcluster_types.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/common/schema.h"

namespace yb::master {

class CDCStreamInfo;

// Map[NamespaceId]:xClusterSafeTime
typedef std::unordered_map<NamespaceId, HybridTime> XClusterNamespaceToSafeTimeMap;

struct NamespaceCheckpointInfo {
  bool initial_bootstrap_required = false;
  struct TableInfo {
    TableId table_id;
    xrepl::StreamId stream_id;
    TableName table_name;
    PgSchemaName pg_schema_name;
    bool operator==(const TableInfo& rhs) const {
      return table_id == rhs.table_id && stream_id == rhs.stream_id &&
             table_name == rhs.table_name && pg_schema_name == rhs.pg_schema_name;
    }
  };
  std::vector<TableInfo> table_infos;
};

YB_DEFINE_ENUM(StreamCheckpointLocation, (kOpId0)(kCurrentEndOfWAL));

using XClusterCheckpointStreamsResult = Result<std::pair<std::vector<TableId>, bool>>;

class XClusterCreateStreamsContext {
 public:
  XClusterCreateStreamsContext() = default;
  virtual ~XClusterCreateStreamsContext() = default;

  virtual void Commit() {}

  std::vector<scoped_refptr<CDCStreamInfo>> streams_;

 private:
  DISALLOW_COPY_AND_ASSIGN(XClusterCreateStreamsContext);
};

using XClusterCheckpointStreamsResult = Result<std::pair<std::vector<TableId>, bool>>;

}  // namespace yb::master
