// Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/remote_bootstrap_session-test.h"

#include <memory>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"
#include "yb/consensus/state_change_context.h"

#include "yb/gutil/bind.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/backoff_waiter.h"

using std::string;

namespace yb {
namespace tserver {

void RemoteBootstrapSessionTest::SetUp() {
  ASSERT_OK(ThreadPoolBuilder("raft").Build(&raft_pool_));
  raft_notifications_pool_ = std::make_unique<rpc::ThreadPool>(rpc::ThreadPoolOptions {
    .name = "raft_notifications",
    .max_workers = rpc::ThreadPoolOptions::kUnlimitedWorkers
  });
  ASSERT_OK(ThreadPoolBuilder("prepare").Build(&tablet_prepare_pool_));
  ASSERT_OK(ThreadPoolBuilder("log").Build(&log_thread_pool_));
  YBTabletTest::SetUp();
  SetUpTabletPeer();
  ASSERT_NO_FATALS(PopulateTablet());
  InitSession();
}

void RemoteBootstrapSessionTest::TearDown() {
  multi_raft_manager_->StartShutdown();
  messenger_->Shutdown();
  session_.reset();
  WARN_NOT_OK(
    tablet_peer_->Shutdown(
        tablet::ShouldAbortActiveTransactions::kTrue, tablet::DisableFlushOnShutdown::kFalse),
    "Tablet peer shutdown failed");
  multi_raft_manager_->CompleteShutdown();
  YBTabletTest::TearDown();
}

void RemoteBootstrapSessionTest::SetUpTabletPeer() {
  scoped_refptr<Log> log;
  auto tablet_ptr = harness_->tablet();
  auto tablet_id = tablet_ptr->tablet_id();
  log::NewSegmentAllocationCallback noop = {};
  auto new_segment_allocation_callback =
      tablet()->metadata()->IsLazySuperblockFlushEnabled()
          ? std::bind(
                &tablet::RaftGroupMetadata::Flush, tablet()->metadata(),
                tablet::OnlyIfDirty::kTrue)
          : noop;
  ASSERT_OK(Log::Open(LogOptions(), tablet_id,
                     fs_manager()->GetFirstTabletWalDirOrDie(tablet_ptr->metadata()->table_id(),
                                                             tablet_id),
                     fs_manager()->uuid(),
                     *tablet()->schema(),
                     0,  // schema_version
                     nullptr, // table_metric_entity
                     nullptr, // tablet_metric_entity
                     log_thread_pool_.get(),
                     log_thread_pool_.get(),
                     log_thread_pool_.get(),
                     std::numeric_limits<int64_t>::max(), // cdc_min_replicated_index
                     &log,
                     {}, // pre_log_rollover_callback,
                     new_segment_allocation_callback));

  scoped_refptr<MetricEntity> table_metric_entity =
    METRIC_ENTITY_table.Instantiate(&metric_registry_, Format("table-$0", CURRENT_TEST_NAME()));
  scoped_refptr<MetricEntity> tablet_metric_entity =
    METRIC_ENTITY_tablet.Instantiate(&metric_registry_, Format("tablet-$0", CURRENT_TEST_NAME()));

  RaftPeerPB config_peer;
  config_peer.set_permanent_uuid(fs_manager()->uuid());
  config_peer.set_member_type(consensus::PeerMemberType::VOTER);
  auto hp = config_peer.mutable_last_known_private_addr()->Add();
  hp->set_host("fake-host");
  hp->set_port(0);

  tablet_peer_.reset(new TabletPeer(
      tablet_ptr->metadata(), config_peer, clock(), fs_manager()->uuid(),
      Bind(
          &RemoteBootstrapSessionTest::TabletPeerStateChangedCallback,
          Unretained(this),
          tablet_id),
      &metric_registry_,
      nullptr /* tablet_splitter */,
      std::shared_future<client::YBClient*>()));

  // TODO similar to code in tablet_peer-test, consider refactor.
  RaftConfigPB config;
  config.add_peers()->CopyFrom(config_peer);
  config.set_opid_index(consensus::kInvalidOpIdIndex);

  std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
      tablet()->metadata()->fs_manager(), tablet_id, fs_manager()->uuid(), config,
      consensus::kMinimumTerm));

  MessengerBuilder mbuilder(CURRENT_TEST_NAME());
  messenger_ = ASSERT_RESULT(mbuilder.Build());
  proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
  multi_raft_manager_ = std::make_unique<consensus::MultiRaftManager>(messenger_.get(),
                                                                      proxy_cache_.get(),
                                                                      config_peer.cloud_info());

  log_anchor_registry_.reset(new LogAnchorRegistry());
  consensus::RetryableRequestsManager retryable_requests_manager(
      tablet_id,
      fs_manager(),
      fs_manager()->GetWalRootDirs()[0],
      MemTracker::FindOrCreateTracker(tablet_id),
      "");
  Status s = retryable_requests_manager.Init(clock());
  ASSERT_OK(tablet_peer_->SetBootstrapping());
  ASSERT_OK(tablet_peer_->InitTabletPeer(
      tablet(),
      nullptr /* server_mem_tracker */,
      messenger_.get(),
      proxy_cache_.get(),
      log,
      table_metric_entity,
      tablet_metric_entity,
      raft_pool_.get(),
      raft_notifications_pool_.get(),
      tablet_prepare_pool_.get(),
      &retryable_requests_manager,
      nullptr /* consensus_meta */,
      multi_raft_manager_.get(),
      nullptr /* flush_retryable_requests_pool */));
  consensus::ConsensusBootstrapInfo boot_info;
  ASSERT_OK(tablet_peer_->Start(boot_info));

  ASSERT_OK(tablet_peer_->WaitUntilConsensusRunning(MonoDelta::FromSeconds(2)));


  ASSERT_OK(LoggedWaitFor([&]() -> Result<bool> {
    if (FLAGS_quick_leader_election_on_create) {
      return tablet_peer_->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
    }
    RETURN_NOT_OK(VERIFY_RESULT(tablet_peer_->GetConsensus())->EmulateElection());
    return true;
  }, MonoDelta::FromMilliseconds(500), "If quick leader elections enabled, wait for peer to be a "
                                       "leader, otherwise emulate."));
}

void RemoteBootstrapSessionTest::TabletPeerStateChangedCallback(
    const string& tablet_id, std::shared_ptr<consensus::StateChangeContext> context) {
  LOG(INFO) << "Tablet peer state changed for tablet " << tablet_id
            << ". Reason: " << context->ToString();
}

void RemoteBootstrapSessionTest::PopulateTablet() {
  auto tablet_ptr = ASSERT_RESULT(tablet_peer_->shared_tablet_safe());
  for (int32_t i = 0; i < 1000; i++) {
    WriteRequestPB req;
    req.set_tablet_id(tablet_peer_->tablet_id());
    AddTestRowInsert(i, i * 2, Substitute("key$0", i), &req);

    WriteResponsePB resp;
    CountDownLatch latch(1);

    auto query = std::make_unique<tablet::WriteQuery>(
        kLeaderTerm, CoarseTimePoint::max() /* deadline */, tablet_peer_.get(),
        tablet_ptr, nullptr, &resp);
    query->set_client_request(req);
    query->set_callback(tablet::MakeLatchOperationCompletionCallback(&latch, &resp));
    tablet_peer_->WriteAsync(std::move(query));
    latch.Wait();
    ASSERT_FALSE(resp.has_error()) << "Request failed: " << resp.error().ShortDebugString();
    ASSERT_EQ(QLResponsePB::YQL_STATUS_OK, resp.ql_response_batch(0).status()) <<
        "Insert error: " << resp.ShortDebugString();
  }
  ASSERT_OK(tablet()->Flush(tablet::FlushMode::kSync));
}

void RemoteBootstrapSessionTest::InitSession() {
  session_.reset(new RemoteBootstrapSession(
      tablet_peer_, "TestSession", "FakeUUID", nullptr /* nsessions */));
  ASSERT_OK(session_->InitBootstrapSession());
}

}  // namespace tserver
}  // namespace yb
