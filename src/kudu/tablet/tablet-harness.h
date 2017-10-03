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
#ifndef KUDU_TABLET_TABLET_PEER_HARNESS_H
#define KUDU_TABLET_TABLET_PEER_HARNESS_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/server/logical_clock.h"
#include "kudu/server/metadata.h"
#include "kudu/tablet/tablet.h"
#include "kudu/util/env.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/status.h"

using std::string;
using std::vector;

namespace kudu {
namespace tablet {

// Creates a default partition schema and partition for a table.
//
// The provided schema must include column IDs.
//
// The partition schema will have no hash components, and a single range
// component over the primary key columns. The partition will cover the
// entire partition-key space.
static std::pair<PartitionSchema, Partition> CreateDefaultPartition(const Schema& schema) {
  // Create a default partition schema.
  PartitionSchema partition_schema;
  CHECK_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  // Create the tablet partitions.
  vector<Partition> partitions;
  CHECK_OK(partition_schema.CreatePartitions(vector<KuduPartialRow>(), schema, &partitions));
  CHECK_EQ(1, partitions.size());
  return std::make_pair(partition_schema, partitions[0]);
}

class TabletHarness {
 public:
  struct Options {
    explicit Options(string root_dir)
        : env(Env::Default()),
          tablet_id("test_tablet_id"),
          root_dir(std::move(root_dir)),
          enable_metrics(true) {}

    Env* env;
    string tablet_id;
    string root_dir;
    bool enable_metrics;
  };

  TabletHarness(const Schema& schema, Options options)
      : options_(std::move(options)), schema_(schema) {}

  Status Create(bool first_time) {
    std::pair<PartitionSchema, Partition> partition(CreateDefaultPartition(schema_));

    // Build the Tablet
    fs_manager_.reset(new FsManager(options_.env, options_.root_dir));
    if (first_time) {
      RETURN_NOT_OK(fs_manager_->CreateInitialFileSystemLayout());
    }
    RETURN_NOT_OK(fs_manager_->Open());

    scoped_refptr<TabletMetadata> metadata;
    RETURN_NOT_OK(TabletMetadata::LoadOrCreate(fs_manager_.get(),
                                               options_.tablet_id,
                                               "KuduTableTest",
                                               schema_,
                                               partition.first,
                                               partition.second,
                                               TABLET_DATA_READY,
                                               &metadata));
    if (options_.enable_metrics) {
      metrics_registry_.reset(new MetricRegistry());
    }

    clock_ = server::LogicalClock::CreateStartingAt(Timestamp::kInitialTimestamp);
    tablet_.reset(new Tablet(metadata,
                             clock_,
                             std::shared_ptr<MemTracker>(),
                             metrics_registry_.get(),
                             new log::LogAnchorRegistry()));
    return Status::OK();
  }

  Status Open() {
    RETURN_NOT_OK(tablet_->Open());
    tablet_->MarkFinishedBootstrapping();
    return Status::OK();
  }

  server::Clock* clock() const {
    return clock_.get();
  }

  const std::shared_ptr<Tablet>& tablet() {
    return tablet_;
  }

  FsManager* fs_manager() {
    return fs_manager_.get();
  }

  MetricRegistry* metrics_registry() {
    return metrics_registry_.get();
  }

 private:
  Options options_;

  gscoped_ptr<MetricRegistry> metrics_registry_;

  scoped_refptr<server::Clock> clock_;
  Schema schema_;
  gscoped_ptr<FsManager> fs_manager_;
  std::shared_ptr<Tablet> tablet_;
};

} // namespace tablet
} // namespace kudu
#endif /* KUDU_TABLET_TABLET_PEER_HARNESS_H */
