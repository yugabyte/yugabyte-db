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

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids_types.h"

#include "yb/master/catalog_entity_tasks.h"
#include "yb/master/master_fwd.h"

namespace yb::master {

class Master;

// Multi-step task to handle a new producer schema for an automatic xCluster target table.
// This task queries all tablets of a table for their highest compatible schema version, and if they
// all match, updates the producer->consumer schema mapping (producer version -> compatible consumer
// version).
// If there isn't a common compatible version, this task inserts a packed schema for this new
// producer schema into the consumer tablet's historical set of schemas and retries.
class HandleNewSchemaForAutomaticXClusterTargetTask : public MultiStepTableTaskBase {
 public:
  // create_async_insert_packed_schema_tasks_fn: Creates tasks to insert a packed schema for a given
  // schema into the consumer tablet's historical set of schemas.
  HandleNewSchemaForAutomaticXClusterTargetTask(
      CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      TableInfoPtr table_info, const LeaderEpoch& epoch, Master& master,
      const xcluster::ReplicationGroupId& replication_group_id, const xrepl::StreamId& stream_id,
      uint32_t producer_schema_version, const SchemaPB& schema,
      std::function<Status(const TableId&, const SchemaPB&, uint32_t)>
          create_async_insert_packed_schema_tasks_fn);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kXClusterHandleNewSchema;
  }

  std::string type_name() const override {
    return "Handle New Schema For Automatic XCluster Target";
  }

  std::string description() const override;

 private:
  Status FirstStep() override;
  Status CheckResponses();
  Status UpdateSchemaVersionMapping(uint32_t compatible_schema_version);
  Status InsertPackedSchema();
  Status WaitForInsertPackedSchemaTasks();

  const xcluster::ReplicationGroupId replication_group_id_;
  const xrepl::StreamId stream_id_;
  const uint32_t producer_schema_version_;
  const SchemaPB schema_;
  Master& master_;

  std::function<Status(const TableId&, const SchemaPB&, uint32_t)>
      create_async_insert_packed_schema_tasks_fn_;

  std::mutex mutex_;
  struct TabletResponse {
    Status status;
    uint32_t latest_compatible_schema_version;
    TabletId tablet_id;
  };
  std::vector<TabletResponse> tablet_responses_ GUARDED_BY(mutex_);
  size_t pending_responses_count_ GUARDED_BY(mutex_);
};

}  // namespace yb::master
