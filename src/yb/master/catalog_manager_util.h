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

#include "yb/common/common.fwd.h"
#include "yb/common/common_net.fwd.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"

#include "yb/gutil/macros.h"

#include "yb/master/catalog_entity_info.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Schema;

namespace dockv {
class PartitionSchema;
}  // namespace dockv

namespace tablet {
class TableInfoPB;
}  // namespace tablet

namespace master {

class CatalogManagerUtil {
 public:
  enum CloudInfoSimilarity {
    NO_MATCH = 0,
    CLOUD_MATCH = 1,
    REGION_MATCH = 2,
    ZONE_MATCH = 3
  };

  // Computes a similarity score between two cloudinfos (which may be prefixes).
  // 0: different clouds
  // 1: same cloud, different region
  // 2: same cloud and region, different zone
  // 3: same cloud and region and zone, or prefix matches
  static CloudInfoSimilarity ComputeCloudInfoSimilarity(const CloudInfoPB& ci1,
                                                        const CloudInfoPB& ci2);

  static void FillTableInfoPB(
      const TableId& table_id, const std::string& table_name, const TableType& table_type,
      const Schema& schema, uint32_t schema_version, const dockv::PartitionSchema& partition_schema,
      tablet::TableInfoPB* pb);

  static void FillTableInfoPB(
      const TableId& table_id, const std::string& table_name, const TableType& table_type,
      const SchemaPB& schema, uint32_t schema_version, const PartitionSchemaPB& partition_schema,
      tablet::TableInfoPB* pb);

 private:
  CatalogManagerUtil();

  DISALLOW_COPY_AND_ASSIGN(CatalogManagerUtil);
};

template <class PB>
bool IsIndex(const PB& pb) {
  return pb.has_index_info() || !pb.indexed_table_id().empty();
}

inline bool IsTable(const SysTablesEntryPB& pb) {
  return !IsIndex(pb);
}

// The indexed table of an index, from index_info or from the deprecated indexed_table_id.
const std::string& GetIndexedTableId(const SysTablesEntryPB& pb);

// Tablets cannot open a vector index with a deprecated (non-block-based) or unset backend, see
// HnswBackend. Returns NotSupported for such an index, so that a restore that would bring one back
// is rejected before it changes the cluster.
Status ValidateVectorIndexBackend(const TableId& table_id, const SysTablesEntryPB& table);

} // namespace master
} // namespace yb
