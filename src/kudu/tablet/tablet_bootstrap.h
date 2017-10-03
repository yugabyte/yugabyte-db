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
#ifndef KUDU_TABLET_TABLET_BOOTSTRAP_H_
#define KUDU_TABLET_TABLET_BOOTSTRAP_H_

#include <boost/thread/shared_mutex.hpp>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/schema.h"
#include "kudu/consensus/log.pb.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/status.h"

namespace kudu {

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
class TabletMetadata;

// A listener for logging the tablet related statuses as well as
// piping it into the web UI.
class TabletStatusListener {
 public:
  explicit TabletStatusListener(const scoped_refptr<TabletMetadata>& meta);

  ~TabletStatusListener();

  void StatusMessage(const std::string& status);

  const std::string tablet_id() const;

  const std::string table_name() const;

  const Partition& partition() const;

  const Schema& schema() const;

  std::string last_status() const {
    boost::shared_lock<boost::shared_mutex> l(lock_);
    return last_status_;
  }

 private:
  mutable boost::shared_mutex lock_;

  scoped_refptr<TabletMetadata> meta_;
  std::string last_status_;

  DISALLOW_COPY_AND_ASSIGN(TabletStatusListener);
};

extern const char* kLogRecoveryDir;

// Bootstraps a tablet, initializing it with the provided metadata. If the tablet
// has blocks and log segments, this method rebuilds the soft state by replaying
// the Log.
//
// This is a synchronous method, but is typically called within a thread pool by
// TSTabletManager.
Status BootstrapTablet(const scoped_refptr<TabletMetadata>& meta,
                       const scoped_refptr<server::Clock>& clock,
                       const std::shared_ptr<MemTracker>& mem_tracker,
                       MetricRegistry* metric_registry,
                       TabletStatusListener* status_listener,
                       std::shared_ptr<Tablet>* rebuilt_tablet,
                       scoped_refptr<log::Log>* rebuilt_log,
                       const scoped_refptr<log::LogAnchorRegistry>& log_anchor_registry,
                       consensus::ConsensusBootstrapInfo* consensus_info);

}  // namespace tablet
}  // namespace kudu

#endif /* KUDU_TABLET_TABLET_BOOTSTRAP_H_ */
