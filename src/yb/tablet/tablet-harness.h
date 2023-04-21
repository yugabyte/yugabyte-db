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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yb/common/schema.h"

#include "yb/dockv/dockv_fwd.h"

#include "yb/fs/fs_manager.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/env.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"


namespace yb {
namespace tablet {

// Creates a default partition schema and partition for a table.
//
// The provided schema must include column IDs.
//
// The partition schema will have no hash components, and a single range component over the primary
// key columns. The partition will cover the entire partition-key space.
std::pair<dockv::PartitionSchema, dockv::Partition> CreateDefaultPartition(const Schema& schema);

class TabletHarness {
 public:
  struct Options {
    explicit Options(std::string root_dir)
        : env(Env::Default()),
          tablet_id("test_tablet_id"),
          root_dir(std::move(root_dir)),
          table_type(TableType::DEFAULT_TABLE_TYPE),
          enable_metrics(true) {}

    Env* env;
    std::string tablet_id;
    std::string root_dir;
    TableType table_type;
    bool enable_metrics;
  };

  TabletHarness(const Schema& schema, Options options)
      : options_(std::move(options)), schema_(schema) {}

  virtual ~TabletHarness() = default;

  Status Create(bool first_time);

  Status Open();

  Result<TabletPtr> OpenTablet(const TabletId& tablet_id);

  TabletInitData MakeTabletInitData(const RaftGroupMetadataPtr& metadata);

  server::Clock* clock() const {
    return clock_.get();
  }

  const TabletPtr& tablet() {
    return tablet_;
  }

  FsManager* fs_manager() {
    return fs_manager_.get();
  }

  MetricRegistry* metrics_registry() {
    return metrics_registry_.get();
  }

  const Options& options() const { return options_; }

 private:
  Options options_;

  std::unique_ptr<MetricRegistry> metrics_registry_;

  scoped_refptr<server::Clock> clock_;
  Schema schema_;
  std::unique_ptr<FsManager> fs_manager_;
  TabletPtr tablet_;
};

} // namespace tablet
} // namespace yb
