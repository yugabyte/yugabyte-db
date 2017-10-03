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

#include <gtest/gtest.h>

#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/consensus/consensus_peers.h"
#include "kudu/consensus/consensus-test-util.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

METRIC_DECLARE_entity(tablet);

namespace kudu {
namespace consensus {

using log::Log;
using log::LogOptions;
using log::LogAnchorRegistry;

const char* kTabletId = "test-peers-tablet";
const char* kLeaderUuid = "peer-0";
const char* kFollowerUuid = "peer-1";

class ConsensusPeersTest : public KuduTest {
 public:
  ConsensusPeersTest()
    : metric_entity_(METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "peer-test")),
      schema_(GetSimpleTestSchema()) {
    CHECK_OK(ThreadPoolBuilder("test-peer-pool").set_max_threads(1).Build(&pool_));
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
    fs_manager_.reset(new FsManager(env_.get(), GetTestPath("fs_root")));
    CHECK_OK(fs_manager_->CreateInitialFileSystemLayout());
    CHECK_OK(Log::Open(options_,
                       fs_manager_.get(),
                       kTabletId,
                       schema_,
                       0, // schema_version
                       NULL,
                       &log_));
    clock_.reset(new server::HybridClock());
    ASSERT_OK(clock_->Init());

    consensus_.reset(new TestRaftConsensusQueueIface());
    message_queue_.reset(new PeerMessageQueue(metric_entity_,
                                              log_.get(),
                                              FakeRaftPeerPB(kLeaderUuid),
                                              kTabletId));
    message_queue_->RegisterObserver(consensus_.get());
  }

  virtual void TearDown() OVERRIDE {
    CHECK_OK(log_->WaitUntilAllFlushed());
  }

  DelayablePeerProxy<NoOpTestPeerProxy>* NewRemotePeer(
      const string& peer_name,
      gscoped_ptr<Peer>* peer) {
    RaftPeerPB peer_pb;
    peer_pb.set_permanent_uuid(peer_name);
    auto proxy_ptr = new DelayablePeerProxy<NoOpTestPeerProxy>(
        pool_.get(), new NoOpTestPeerProxy(pool_.get(), peer_pb));
    gscoped_ptr<PeerProxy> proxy(proxy_ptr);
    CHECK_OK(Peer::NewRemotePeer(peer_pb,
                                 kTabletId,
                                 kLeaderUuid,
                                 message_queue_.get(),
                                 pool_.get(),
                                 proxy.Pass(),
                                 peer));
    return proxy_ptr;
  }

  void CheckLastLogEntry(int term, int index) {
    OpId id;
    log_->GetLatestEntryOpId(&id);
    ASSERT_EQ(id.term(), term);
    ASSERT_EQ(id.index(), index);
  }

  void CheckLastRemoteEntry(DelayablePeerProxy<NoOpTestPeerProxy>* proxy, int term, int index) {
    OpId id;
    id.CopyFrom(proxy->proxy()->last_received());
    ASSERT_EQ(id.term(), term);
    ASSERT_EQ(id.index(), index);
  }

  // Registers a callback triggered when the op with the provided term and index
  // is committed in the test consensus impl.
  // This must be called _before_ the operation is committed.
  void WaitForMajorityReplicatedIndex(int index) {
    for (int i = 0; i < 100; i++) {
      if (consensus_->IsMajorityReplicated(index)) {
        return;
      }
      SleepFor(MonoDelta::FromMilliseconds(i));
    }
    FAIL() << "Never replicated index " << index << " on a majority";
  }

 protected:
  gscoped_ptr<TestRaftConsensusQueueIface> consensus_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  gscoped_ptr<FsManager> fs_manager_;
  scoped_refptr<Log> log_;
  gscoped_ptr<PeerMessageQueue> message_queue_;
  const Schema schema_;
  LogOptions options_;
  gscoped_ptr<ThreadPool> pool_;
  scoped_refptr<server::Clock> clock_;
};


// Tests that a remote peer is correctly built and tracked
// by the message queue.
// After the operations are considered done the proxy (which
// simulates the other endpoint) should reflect the replicated
// messages.
TEST_F(ConsensusPeersTest, TestRemotePeer) {
  // We use a majority size of 2 since we make one fake remote peer
  // in addition to our real local log.
  message_queue_->Init(MinimumOpId());
  message_queue_->SetLeaderMode(MinimumOpId(),
                                MinimumOpId().term(),
                                BuildRaftConfigPBForTests(3));

  gscoped_ptr<Peer> remote_peer;
  DelayablePeerProxy<NoOpTestPeerProxy>* proxy =
      NewRemotePeer(kFollowerUuid, &remote_peer);

  // Append a bunch of messages to the queue
  AppendReplicateMessagesToQueue(message_queue_.get(), clock_, 1, 20);

  // The above append ends up appending messages in term 2, so we
  // update the peer's term to match.
  remote_peer->SetTermForTest(2);

  // signal the peer there are requests pending.
  remote_peer->SignalRequest();
  // now wait on the status of the last operation
  // this will complete once the peer has logged all
  // requests.
  WaitForMajorityReplicatedIndex(20);
  // verify that the replicated watermark corresponds to the last replicated
  // message.
  CheckLastRemoteEntry(proxy, 2, 20);
}

TEST_F(ConsensusPeersTest, TestRemotePeers) {
  message_queue_->Init(MinimumOpId());
  message_queue_->SetLeaderMode(MinimumOpId(),
                                MinimumOpId().term(),
                                BuildRaftConfigPBForTests(3));

  // Create a set of remote peers
  gscoped_ptr<Peer> remote_peer1;
  DelayablePeerProxy<NoOpTestPeerProxy>* remote_peer1_proxy =
      NewRemotePeer("peer-1", &remote_peer1);

  gscoped_ptr<Peer> remote_peer2;
  DelayablePeerProxy<NoOpTestPeerProxy>* remote_peer2_proxy =
      NewRemotePeer("peer-2", &remote_peer2);

  // Delay the response from the second remote peer.
  remote_peer2_proxy->DelayResponse();

  // Append one message to the queue.
  AppendReplicateMessagesToQueue(message_queue_.get(), clock_, 1, 1);

  OpId first = MakeOpId(0, 1);

  remote_peer1->SignalRequest();
  remote_peer2->SignalRequest();

  // Now wait for the message to be replicated, this should succeed since
  // majority = 2 and only one peer was delayed. The majority is made up
  // of remote-peer1 and the local log.
  WaitForMajorityReplicatedIndex(first.index());

  CheckLastLogEntry(first.term(), first.index());
  CheckLastRemoteEntry(remote_peer1_proxy, first.term(), first.index());

  remote_peer2_proxy->Respond(TestPeerProxy::kUpdate);
  // Wait until all peers have replicated the message, otherwise
  // when we add the next one remote_peer2 might find the next message
  // in the queue and will replicate it, which is not what we want.
  while (!OpIdEquals(message_queue_->GetAllReplicatedIndexForTests(), first)) {
    SleepFor(MonoDelta::FromMilliseconds(1));
  }

  // Now append another message to the queue
  AppendReplicateMessagesToQueue(message_queue_.get(), clock_, 2, 1);

  // We should not see it replicated, even after 10ms,
  // since only the local peer replicates the message.
  SleepFor(MonoDelta::FromMilliseconds(10));
  ASSERT_FALSE(consensus_->IsMajorityReplicated(2));

  // Signal one of the two remote peers.
  remote_peer1->SignalRequest();
  // We should now be able to wait for it to replicate, since two peers (a majority)
  // have replicated the message.
  WaitForMajorityReplicatedIndex(2);
}

// Regression test for KUDU-699: even if a peer isn't making progress,
// and thus always has data pending, we should be able to close the peer.
TEST_F(ConsensusPeersTest, TestCloseWhenRemotePeerDoesntMakeProgress) {
  message_queue_->Init(MinimumOpId());
  message_queue_->SetLeaderMode(MinimumOpId(),
                                MinimumOpId().term(),
                                BuildRaftConfigPBForTests(3));

  auto mock_proxy = new MockedPeerProxy(pool_.get());
  gscoped_ptr<Peer> peer;
  ASSERT_OK(Peer::NewRemotePeer(FakeRaftPeerPB(kFollowerUuid),
                                kTabletId,
                                kLeaderUuid,
                                message_queue_.get(),
                                pool_.get(),
                                gscoped_ptr<PeerProxy>(mock_proxy),
                                &peer));

  // Make the peer respond without making any progress -- it always returns
  // that it has only replicated op 0.0. When we see the response, we always
  // decide that more data is pending, and we want to send another request.
  ConsensusResponsePB peer_resp;
  peer_resp.set_responder_uuid(kFollowerUuid);
  peer_resp.set_responder_term(0);
  peer_resp.mutable_status()->mutable_last_received()->CopyFrom(
      MakeOpId(0, 0));
  peer_resp.mutable_status()->mutable_last_received_current_leader()->CopyFrom(
      MakeOpId(0, 0));
  peer_resp.mutable_status()->set_last_committed_idx(0);

  mock_proxy->set_update_response(peer_resp);

  // Add an op to the queue and start sending requests to the peer.
  AppendReplicateMessagesToQueue(message_queue_.get(), clock_, 1, 1);
  peer->SignalRequest(true);

  // We should be able to close the peer even though it has more data pending.
  peer->Close();
}

TEST_F(ConsensusPeersTest, TestDontSendOneRpcPerWriteWhenPeerIsDown) {
  message_queue_->Init(MinimumOpId());
  message_queue_->SetLeaderMode(MinimumOpId(),
                                MinimumOpId().term(),
                                BuildRaftConfigPBForTests(3));

  auto mock_proxy = new MockedPeerProxy(pool_.get());
  gscoped_ptr<Peer> peer;
  ASSERT_OK(Peer::NewRemotePeer(FakeRaftPeerPB(kFollowerUuid),
                                kTabletId,
                                kLeaderUuid,
                                message_queue_.get(),
                                pool_.get(),
                                gscoped_ptr<PeerProxy>(mock_proxy),
                                &peer));

  // Initial response has to be successful -- otherwise we'll consider the peer
  // "new" and only send heartbeat RPCs.
  ConsensusResponsePB initial_resp;
  initial_resp.set_responder_uuid(kFollowerUuid);
  initial_resp.set_responder_term(0);
  initial_resp.mutable_status()->mutable_last_received()->CopyFrom(
      MakeOpId(1, 1));
  initial_resp.mutable_status()->mutable_last_received_current_leader()->CopyFrom(
      MakeOpId(1, 1));
  initial_resp.mutable_status()->set_last_committed_idx(0);
  mock_proxy->set_update_response(initial_resp);

  AppendReplicateMessagesToQueue(message_queue_.get(), clock_, 1, 1);
  peer->SignalRequest(true);

  // Now wait for the message to be replicated, this should succeed since
  // the local (leader) peer always acks and the follower also acked this time.
  WaitForMajorityReplicatedIndex(1);

  // Set up the peer to respond with an error.
  ConsensusResponsePB error_resp;
  error_resp.mutable_error()->set_code(tserver::TabletServerErrorPB::UNKNOWN_ERROR);
  StatusToPB(Status::NotFound("fake error"), error_resp.mutable_error()->mutable_status());
  mock_proxy->set_update_response(error_resp);

  // Add a bunch of messages to the queue.
  for (int i = 2; i <= 100; i++) {
    AppendReplicateMessagesToQueue(message_queue_.get(), clock_, i, 1);
    peer->SignalRequest(false);
    SleepFor(MonoDelta::FromMilliseconds(2));
  }

  // Check that we didn't attempt to send one UpdateConsensus call per
  // Write. 100 writes might have taken a second or two, though, so it's
  // OK to have called UpdateConsensus() a few times due to regularly
  // scheduled heartbeats.
  ASSERT_LT(mock_proxy->update_count(), 5);
}

}  // namespace consensus
}  // namespace kudu

