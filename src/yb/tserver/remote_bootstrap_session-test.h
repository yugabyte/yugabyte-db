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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <gflags/gflags.h>
#include <stdint.h>
#include <memory>
#include <string>

#include "yb/common/wire_protocol-test-util.h"
#include "yb/consensus/multi_raft_batcher.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tserver/remote_bootstrap_session.h"
#include "yb/util/metrics.h"
#include "yb/util/threadpool.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/util/thread_pool.h"

namespace yb {
enum TableType : int;
namespace consensus {
class ConsensusMetadata;
class RaftConfigPB;
class RaftPeerPB;
struct StateChangeContext;
}  // namespace consensus
namespace log {
class Log;
struct LogOptions;
}  // namespace log
namespace tablet {
class RaftGroupReplicaSuperBlockPB;
class TabletPeer;
}  // namespace tablet
}  // namespace yb

METRIC_DECLARE_entity(table);
METRIC_DECLARE_entity(tablet);

DECLARE_bool(quick_leader_election_on_create);


namespace yb {
namespace tserver {

using consensus::ConsensusMetadata;
using consensus::RaftConfigPB;
using consensus::RaftPeerPB;
using log::Log;
using log::LogOptions;
using log::LogAnchorRegistry;
using rpc::Messenger;
using rpc::MessengerBuilder;
using strings::Substitute;
using tablet::YBTabletTest;
using tablet::TabletPeer;
using tablet::RaftGroupReplicaSuperBlockPB;

const int64_t kLeaderTerm = 1;

class RemoteBootstrapSessionTest : public YBTabletTest {
 public:
  explicit RemoteBootstrapSessionTest(TableType table_type)
    : YBTabletTest(GetSimpleTestSchema(), table_type) {
  }

  void SetUp() override;

  void TearDown() override;

 protected:
  void SetUpTabletPeer();

  void TabletPeerStateChangedCallback(const std::string& tablet_id,
                                      std::shared_ptr<consensus::StateChangeContext> context);

  void PopulateTablet();

  virtual void InitSession();

  MetricRegistry metric_registry_;
  scoped_refptr<LogAnchorRegistry> log_anchor_registry_;
  std::unique_ptr<ThreadPool> raft_pool_;
  std::unique_ptr<rpc::ThreadPool> raft_notifications_pool_;
  std::unique_ptr<ThreadPool> tablet_prepare_pool_;
  std::unique_ptr<ThreadPool> log_thread_pool_;
  std::shared_ptr<TabletPeer> tablet_peer_;
  scoped_refptr<RemoteBootstrapSession> session_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<consensus::MultiRaftManager> multi_raft_manager_;
};

}  // namespace tserver
}  // namespace yb
