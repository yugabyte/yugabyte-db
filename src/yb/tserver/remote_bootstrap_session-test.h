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

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/consensus/consensus_fwd.h"
#include "yb/dockv/partial_row.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/multi_raft_batcher.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tablet/tablet-test-util.h"

#include "yb/tserver/remote_bootstrap_session.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/util/threadpool.h"

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
