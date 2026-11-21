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

#include "yb/master/catalog_manager_util.h"

#include "yb/common/common.pb.h"
#include "yb/common/common_net.pb.h"
#include "yb/common/schema_pbutil.h"

#include "yb/dockv/partition.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_error.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/status_format.h"

namespace yb {
namespace master {

CatalogManagerUtil::CloudInfoSimilarity CatalogManagerUtil::ComputeCloudInfoSimilarity(
    const CloudInfoPB& ci1, const CloudInfoPB& ci2) {
  if (ci1.has_placement_cloud() &&
      ci2.has_placement_cloud() &&
      ci1.placement_cloud() != ci2.placement_cloud()) {
      return NO_MATCH;
  }

  if (ci1.has_placement_region() &&
      ci2.has_placement_region() &&
      ci1.placement_region() != ci2.placement_region()) {
      return CLOUD_MATCH;
  }

  if (ci1.has_placement_zone() &&
      ci2.has_placement_zone() &&
      ci1.placement_zone() != ci2.placement_zone()) {
      return REGION_MATCH;
  }
  return ZONE_MATCH;
}

void CatalogManagerUtil::FillTableInfoPB(
    const TableId& table_id, const std::string& table_name, const TableType& table_type,
    const Schema& schema, uint32_t schema_version, const dockv::PartitionSchema& partition_schema,
    tablet::TableInfoPB* pb) {
  SchemaPB schema_pb;
  SchemaToPB(schema, &schema_pb);
  PartitionSchemaPB partition_schema_pb;
  partition_schema.ToPB(&partition_schema_pb);
  FillTableInfoPB(
      table_id, table_name, table_type, schema_pb, schema_version, partition_schema_pb, pb);
}

void CatalogManagerUtil::FillTableInfoPB(
    const TableId& table_id, const std::string& table_name, const TableType& table_type,
    const SchemaPB& schema, uint32_t schema_version, const PartitionSchemaPB& partition_schema,
    tablet::TableInfoPB* pb) {
  pb->set_table_id(table_id);
  pb->set_table_name(table_name);
  pb->set_table_type(table_type);
  pb->mutable_schema()->CopyFrom(schema);
  pb->set_schema_version(schema_version);
  pb->mutable_partition_schema()->CopyFrom(partition_schema);
}

const std::string& GetIndexedTableId(const SysTablesEntryPB& pb) {
  return pb.has_index_info() ? pb.index_info().indexed_table_id() : pb.indexed_table_id();
}

Status ValidateVectorIndexBackend(const TableId& table_id, const SysTablesEntryPB& table) {
  if (!table.has_index_info() || !table.index_info().has_vector_idx_options() ||
      table.index_info().vector_idx_options().idx_type() != PgVectorIndexType::HNSW) {
    return Status::OK();
  }
  const auto backend = table.index_info().vector_idx_options().hnsw().backend();
  if (backend == HnswBackend::YB_HNSW_USEARCH || backend == HnswBackend::YB_HNSW_HNSWLIB) {
    return Status::OK();
  }
  return STATUS(
      NotSupported,
      Format("Vector index $0 ($1) uses backend $2, which is no longer supported",
             table.name(), table_id, HnswBackend_Name(backend)),
      MasterError(MasterErrorPB::SNAPSHOT_FAILED));
}

} // namespace master
} // namespace yb
