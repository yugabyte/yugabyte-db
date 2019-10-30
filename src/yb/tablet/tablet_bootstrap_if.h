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
#ifndef YB_TABLET_TABLET_BOOTSTRAP_IF_H
#define YB_TABLET_TABLET_BOOTSTRAP_IF_H

#include <memory>
#include <string>
#include <vector>

#include <boost/thread/shared_mutex.hpp>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/memory_monitor.h"
#include "yb/client/client_fwd.h"
#include "yb/common/schema.h"
#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/log.pb.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/server/clock.h"
#include "yb/util/status.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/threadpool.h"

namespace yb {

class MetricRegistry;
class Partition;
class PartitionSchema;

namespace log {
class Log;
class LogAnchorRegistry;
}

namespace consensus {
struct ConsensusBootstrapInfo;
} // namespace consensus

namespace server {
class Clock;
}

namespace tablet {
class Tablet;
class RaftGroupMetadata;
class TransactionCoordinatorContext;
class TransactionParticipantContext;
struct TabletOptions;

// A listener for logging the tablet related statuses as well as
// piping it into the web UI.
class TabletStatusListener {
 public:
  explicit TabletStatusListener(const RaftGroupMetadataPtr& meta);

  ~TabletStatusListener();

  void StatusMessage(const std::string& status);

  const std::string tablet_id() const;

  const std::string table_name() const;

  const std::string table_id() const;

  const Partition& partition() const;

  const Schema& schema() const;

  std::string last_status() const {
    SharedLock<boost::shared_mutex> l(lock_);
    return last_status_;
  }

 private:
  mutable boost::shared_mutex lock_;

  RaftGroupMetadataPtr meta_;
  std::string last_status_;

  DISALLOW_COPY_AND_ASSIGN(TabletStatusListener);
};

struct BootstrapTabletData {
  RaftGroupMetadataPtr meta;
  std::shared_future<client::YBClient*> client_future;
  scoped_refptr<server::Clock> clock;
  std::shared_ptr<MemTracker> mem_tracker;
  std::shared_ptr<MemTracker> block_based_table_mem_tracker;
  MetricRegistry* metric_registry;
  TabletStatusListener* listener;
  scoped_refptr<log::LogAnchorRegistry> log_anchor_registry;
  TabletOptions tablet_options;
  std::string log_prefix_suffix;
  TransactionParticipantContext* transaction_participant_context;
  client::LocalTabletFilter local_tablet_filter;
  TransactionCoordinatorContext* transaction_coordinator_context;
  ThreadPool* append_pool;
  consensus::RetryableRequests* retryable_requests;
};

// Bootstraps a tablet, initializing it with the provided metadata. If the tablet
// has blocks and log segments, this method rebuilds the soft state by replaying
// the Log.
//
// This is a synchronous method, but is typically called within a thread pool by
// TSTabletManager.
CHECKED_STATUS BootstrapTablet(
    const BootstrapTabletData& data,
    std::shared_ptr<TabletClass>* rebuilt_tablet,
    scoped_refptr<log::Log>* rebuilt_log,
    consensus::ConsensusBootstrapInfo* consensus_info);

}  // namespace tablet
}  // namespace yb

#endif // YB_TABLET_TABLET_BOOTSTRAP_IF_H
