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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol-test-util.h"
#include "kudu/consensus/consensus_peers.h"
#include "kudu/consensus/consensus-test-util.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/peer_manager.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/server/logical_clock.h"
#include "kudu/util/async_util.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/metrics.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

DECLARE_bool(enable_leader_failure_detection);

METRIC_DECLARE_entity(tablet);

using std::shared_ptr;
using std::string;

namespace kudu {
namespace consensus {

using log::Log;
using log::LogOptions;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::Property;
using ::testing::Return;

const char* kTestTablet = "TestTablet";
const char* kLocalPeerUuid = "peer-0";

// A simple map to collect the results of a sequence of transactions.
typedef std::map<OpId, Status, OpIdCompareFunctor> StatusesMap;

class MockQueue : public PeerMessageQueue {
 public:
  explicit MockQueue(const scoped_refptr<MetricEntity>& metric_entity, log::Log* log)
    : PeerMessageQueue(metric_entity, log, FakeRaftPeerPB(kLocalPeerUuid), kTestTablet) {}
  MOCK_METHOD1(Init, void(const OpId& locally_replicated_index));
  MOCK_METHOD3(SetLeaderMode, void(const OpId& committed_opid,
                                   int64_t current_term,
                                   const RaftConfigPB& active_config));
  MOCK_METHOD0(SetNonLeaderMode, void());
  virtual Status AppendOperations(const vector<ReplicateRefPtr>& msgs,
                                  const StatusCallback& callback) OVERRIDE {
    return AppendOperationsMock(msgs, callback);
  }
  MOCK_METHOD2(AppendOperationsMock, Status(const vector<ReplicateRefPtr>& msgs,
                                            const StatusCallback& callback));
  MOCK_METHOD1(TrackPeer, void(const string&));
  MOCK_METHOD1(UntrackPeer, void(const string&));
  MOCK_METHOD4(RequestForPeer, Status(const std::string& uuid,
                                      ConsensusRequestPB* request,
                                      std::vector<ReplicateRefPtr>* msg_refs,
                                      bool* needs_remote_bootstrap));
  MOCK_METHOD3(ResponseFromPeer, void(const std::string& peer_uuid,
                                      const ConsensusResponsePB& response,
                                      bool* more_pending));
  MOCK_METHOD0(Close, void());
};

class MockPeerManager : public PeerManager {
 public:
  MockPeerManager() : PeerManager("", "", nullptr, nullptr, nullptr, nullptr) {}
  MOCK_METHOD1(UpdateRaftConfig, Status(const consensus::RaftConfigPB& config));
  MOCK_METHOD1(SignalRequest, void(bool force_if_queue_empty));
  MOCK_METHOD0(Close, void());
};

class RaftConsensusSpy : public RaftConsensus {
 public:
  typedef Callback<Status(const scoped_refptr<ConsensusRound>& round)> AppendCallback;

  RaftConsensusSpy(const ConsensusOptions& options,
                   gscoped_ptr<ConsensusMetadata> cmeta,
                   gscoped_ptr<PeerProxyFactory> proxy_factory,
                   gscoped_ptr<PeerMessageQueue> queue,
                   gscoped_ptr<PeerManager> peer_manager,
                   gscoped_ptr<ThreadPool> thread_pool,
                   const scoped_refptr<MetricEntity>& metric_entity,
                   const std::string& peer_uuid,
                   const scoped_refptr<server::Clock>& clock,
                   ReplicaTransactionFactory* txn_factory,
                   const scoped_refptr<log::Log>& log,
                   const shared_ptr<MemTracker>& parent_mem_tracker,
                   const Callback<void(const std::string& reason)>& mark_dirty_clbk)
    : RaftConsensus(options,
                    cmeta.Pass(),
                    proxy_factory.Pass(),
                    queue.Pass(),
                    peer_manager.Pass(),
                    thread_pool.Pass(),
                    metric_entity,
                    peer_uuid,
                    clock,
                    txn_factory,
                    log,
                    parent_mem_tracker,
                    mark_dirty_clbk) {
    // These "aliases" allow us to count invocations and assert on them.
    ON_CALL(*this, StartConsensusOnlyRoundUnlocked(_))
        .WillByDefault(Invoke(this,
              &RaftConsensusSpy::StartNonLeaderConsensusRoundUnlockedConcrete));
    ON_CALL(*this, NonTxRoundReplicationFinished(_, _, _))
        .WillByDefault(Invoke(this, &RaftConsensusSpy::NonTxRoundReplicationFinishedConcrete));
  }

  MOCK_METHOD1(AppendNewRoundToQueueUnlocked, Status(const scoped_refptr<ConsensusRound>& round));
  Status AppendNewRoundToQueueUnlockedConcrete(const scoped_refptr<ConsensusRound>& round) {
    return RaftConsensus::AppendNewRoundToQueueUnlocked(round);
  }

  MOCK_METHOD1(StartConsensusOnlyRoundUnlocked, Status(const ReplicateRefPtr& msg));
  Status StartNonLeaderConsensusRoundUnlockedConcrete(const ReplicateRefPtr& msg) {
    return RaftConsensus::StartConsensusOnlyRoundUnlocked(msg);
  }

  MOCK_METHOD3(NonTxRoundReplicationFinished, void(ConsensusRound* round,
                                                   const StatusCallback& client_cb,
                                                   const Status& status));
  void NonTxRoundReplicationFinishedConcrete(ConsensusRound* round,
                                             const StatusCallback& client_cb,
                                             const Status& status) {
    LOG(INFO) << "Committing round with opid " << round->id()
              << " given Status " << status.ToString();
    RaftConsensus::NonTxRoundReplicationFinished(round, client_cb, status);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(RaftConsensusSpy);
};

void DoNothing(const string& s) {
}

class RaftConsensusTest : public KuduTest {
 public:
  RaftConsensusTest()
      : clock_(server::LogicalClock::CreateStartingAt(Timestamp(0))),
        metric_entity_(METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "raft-consensus-test")),
        schema_(GetSimpleTestSchema()) {
    FLAGS_enable_leader_failure_detection = false;
    options_.tablet_id = kTestTablet;
  }

  virtual void SetUp() OVERRIDE {
    LogOptions options;
    string test_path = GetTestPath("test-peer-root");

    // TODO mock the Log too, since we're gonna mock the queue
    // monitors and pretty much everything else.
    fs_manager_.reset(new FsManager(env_.get(), test_path));
    CHECK_OK(fs_manager_->CreateInitialFileSystemLayout());
    CHECK_OK(fs_manager_->Open());
    CHECK_OK(Log::Open(LogOptions(),
                       fs_manager_.get(),
                       kTestTablet,
                       schema_,
                       0, // schema_version
                       NULL,
                       &log_));

    queue_ = new MockQueue(metric_entity_, log_.get());
    peer_manager_ = new MockPeerManager;
    txn_factory_.reset(new MockTransactionFactory);

    ON_CALL(*queue_, AppendOperationsMock(_, _))
        .WillByDefault(Invoke(this, &RaftConsensusTest::AppendToLog));
  }

  void SetUpConsensus(int64_t initial_term = consensus::kMinimumTerm, int num_peers = 1) {
    config_ = BuildRaftConfigPBForTests(num_peers);
    config_.set_opid_index(kInvalidOpIdIndex);

    gscoped_ptr<PeerProxyFactory> proxy_factory(new LocalTestPeerProxyFactory(nullptr));

    string peer_uuid = config_.peers(num_peers - 1).permanent_uuid();

    gscoped_ptr<ConsensusMetadata> cmeta;
    CHECK_OK(ConsensusMetadata::Create(fs_manager_.get(), kTestTablet, peer_uuid,
                                       config_, initial_term, &cmeta));

    gscoped_ptr<ThreadPool> thread_pool;
    CHECK_OK(ThreadPoolBuilder("raft-pool") .Build(&thread_pool));

    consensus_.reset(new RaftConsensusSpy(options_,
                                          cmeta.Pass(),
                                          proxy_factory.Pass(),
                                          gscoped_ptr<PeerMessageQueue>(queue_),
                                          gscoped_ptr<PeerManager>(peer_manager_),
                                          thread_pool.Pass(),
                                          metric_entity_,
                                          peer_uuid,
                                          clock_,
                                          txn_factory_.get(),
                                          log_.get(),
                                          MemTracker::GetRootTracker(),
                                          Bind(&DoNothing)));

    ON_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
        .WillByDefault(Invoke(this, &RaftConsensusTest::MockAppendNewRound));
  }

  Status AppendToLog(const vector<ReplicateRefPtr>& msgs,
                     const StatusCallback& callback) {
    return log_->AsyncAppendReplicates(msgs,
                                       Bind(LogAppendCallback, callback));
  }

  static void LogAppendCallback(const StatusCallback& callback,
                                const Status& s) {
    CHECK_OK(s);
    callback.Run(s);
  }

  Status MockAppendNewRound(const scoped_refptr<ConsensusRound>& round) {
    rounds_.push_back(round);
    RETURN_NOT_OK(consensus_->AppendNewRoundToQueueUnlockedConcrete(round));
    LOG(INFO) << "Round append: " << round->id() << ", ReplicateMsg: "
              << round->replicate_msg()->ShortDebugString();
    return Status::OK();
  }

  void SetUpGeneralExpectations() {
    EXPECT_CALL(*peer_manager_, SignalRequest(_))
        .Times(AnyNumber());
    EXPECT_CALL(*peer_manager_, Close())
        .Times(AtLeast(1));
    EXPECT_CALL(*queue_, Close())
        .Times(1);
    EXPECT_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
        .Times(AnyNumber());
  }

  // Create a ConsensusRequestPB suitable to send to a peer.
  ConsensusRequestPB MakeConsensusRequest(int64_t caller_term,
                                          const string& caller_uuid,
                                          const OpId& preceding_opid);

  // Add a single no-op with the given OpId to a ConsensusRequestPB.
  void AddNoOpToConsensusRequest(ConsensusRequestPB* request, const OpId& noop_opid);

  scoped_refptr<ConsensusRound> AppendNoOpRound() {
    ReplicateRefPtr replicate_ptr(make_scoped_refptr_replicate(new ReplicateMsg));
    replicate_ptr->get()->set_op_type(NO_OP);
    replicate_ptr->get()->set_timestamp(clock_->Now().ToUint64());
    scoped_refptr<ConsensusRound> round(new ConsensusRound(consensus_.get(), replicate_ptr));
    round->SetConsensusReplicatedCallback(
        Bind(&RaftConsensusSpy::NonTxRoundReplicationFinished,
             Unretained(consensus_.get()), Unretained(round.get()), Bind(&DoNothingStatusCB)));

    CHECK_OK(consensus_->Replicate(round));
    LOG(INFO) << "Appended NO_OP round with opid " << round->id();
    return round;
  }

  void DumpRounds() {
    LOG(INFO) << "Dumping rounds...";
    for (const scoped_refptr<ConsensusRound>& round : rounds_) {
      LOG(INFO) << "Round: OpId " << round->id() << ", ReplicateMsg: "
                << round->replicate_msg()->ShortDebugString();
    }
  }

 protected:
  ConsensusOptions options_;
  RaftConfigPB config_;
  OpId initial_id_;
  gscoped_ptr<FsManager> fs_manager_;
  scoped_refptr<Log> log_;
  gscoped_ptr<PeerProxyFactory> proxy_factory_;
  scoped_refptr<server::Clock> clock_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  const Schema schema_;
  scoped_refptr<RaftConsensusSpy> consensus_;

  vector<scoped_refptr<ConsensusRound> > rounds_;

  // Mocks.
  // NOTE: both 'queue_' and 'peer_manager_' belong to 'consensus_' and may be deleted before
  // the test is.
  MockQueue* queue_;
  MockPeerManager* peer_manager_;
  gscoped_ptr<MockTransactionFactory> txn_factory_;
};

ConsensusRequestPB RaftConsensusTest::MakeConsensusRequest(int64_t caller_term,
                                                           const string& caller_uuid,
                                                           const OpId& preceding_opid) {
  ConsensusRequestPB request;
  request.set_caller_term(caller_term);
  request.set_caller_uuid(caller_uuid);
  request.set_tablet_id(kTestTablet);
  *request.mutable_preceding_id() = preceding_opid;
  return request;
}

void RaftConsensusTest::AddNoOpToConsensusRequest(ConsensusRequestPB* request,
                                                  const OpId& noop_opid) {
  ReplicateMsg* noop_msg = request->add_ops();
  *noop_msg->mutable_id() = noop_opid;
  noop_msg->set_op_type(NO_OP);
  noop_msg->set_timestamp(clock_->Now().ToUint64());
  noop_msg->mutable_noop_request();
}

// Tests that the committed index moves along with the majority replicated
// index when the terms are the same.
TEST_F(RaftConsensusTest, TestCommittedIndexWhenInSameTerm) {
  SetUpConsensus();
  SetUpGeneralExpectations();
  EXPECT_CALL(*peer_manager_, UpdateRaftConfig(_))
      .Times(1)
      .WillOnce(Return(Status::OK()));
  EXPECT_CALL(*queue_, Init(_))
      .Times(1);
  EXPECT_CALL(*queue_, SetLeaderMode(_, _, _))
      .Times(1);
  EXPECT_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
      .Times(11);
  EXPECT_CALL(*queue_, AppendOperationsMock(_, _))
      .Times(11).WillRepeatedly(Return(Status::OK()));

  ConsensusBootstrapInfo info;
  ASSERT_OK(consensus_->Start(info));
  ASSERT_OK(consensus_->EmulateElection());

  // Commit the first noop round, created on EmulateElection();
  OpId committed_index;
  ASSERT_FALSE(rounds_.empty()) << "rounds_ is empty!";
  consensus_->UpdateMajorityReplicated(rounds_[0]->id(), &committed_index);

  ASSERT_OPID_EQ(rounds_[0]->id(), committed_index);

  // Append 10 rounds
  for (int i = 0; i < 10; i++) {
    scoped_refptr<ConsensusRound> round = AppendNoOpRound();
    // queue reports majority replicated index in the leader's term
    // committed index should move accordingly.
    consensus_->UpdateMajorityReplicated(round->id(), &committed_index);
    ASSERT_OPID_EQ(round->id(), committed_index);
  }
}

// Tests that, when terms change, the commit index only advances when the majority
// replicated index is in the current term.
TEST_F(RaftConsensusTest, TestCommittedIndexWhenTermsChange) {
  SetUpConsensus();
  SetUpGeneralExpectations();
  EXPECT_CALL(*peer_manager_, UpdateRaftConfig(_))
      .Times(2)
      .WillRepeatedly(Return(Status::OK()));
  EXPECT_CALL(*queue_, Init(_))
      .Times(1);
  EXPECT_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
      .Times(3);
  EXPECT_CALL(*queue_, AppendOperationsMock(_, _))
      .Times(3).WillRepeatedly(Return(Status::OK()));;

  ConsensusBootstrapInfo info;
  ASSERT_OK(consensus_->Start(info));
  ASSERT_OK(consensus_->EmulateElection());

  OpId committed_index;
  consensus_->UpdateMajorityReplicated(rounds_[0]->id(), &committed_index);
  ASSERT_OPID_EQ(rounds_[0]->id(), committed_index);

  // Append another round in the current term (besides the original config round).
  scoped_refptr<ConsensusRound> round = AppendNoOpRound();

  // Now emulate an election, the same guy will be leader but the term
  // will change.
  ASSERT_OK(consensus_->EmulateElection());

  // Now tell consensus that 'round' has been majority replicated, this _shouldn't_
  // advance the committed index, since that belongs to a previous term.
  OpId new_committed_index;
  consensus_->UpdateMajorityReplicated(round->id(), &new_committed_index);
  ASSERT_OPID_EQ(committed_index, new_committed_index);

  const scoped_refptr<ConsensusRound>& last_config_round = rounds_[2];

  // Now notify that the last change config was committed, this should advance the
  // commit index to the id of the last change config.
  consensus_->UpdateMajorityReplicated(last_config_round->id(), &committed_index);

  DumpRounds();
  ASSERT_OPID_EQ(last_config_round->id(), committed_index);
}

// Asserts that a ConsensusRound has an OpId set in its ReplicateMsg.
MATCHER(HasOpId, "") { return arg->id().IsInitialized(); }

// These matchers assert that a Status object is of a certain type.
MATCHER(IsOk, "") { return arg.ok(); }
MATCHER(IsAborted, "") { return arg.IsAborted(); }

// Tests that consensus is able to handle pending operations. It tests this in two ways:
// - It tests that consensus does the right thing with pending transactions from the the WAL.
// - It tests that when a follower gets promoted to leader it does the right thing
//   with the pending operations.
TEST_F(RaftConsensusTest, TestPendingTransactions) {
  SetUpConsensus(10);

  // Emulate a stateful system by having a bunch of operations in flight when consensus starts.
  // Specifically we emulate we're on term 10, with 5 operations before the last known
  // committed operation, 10.104, which should be committed immediately, and 5 operations after the
  // last known committed operation, which should be pending but not yet committed.
  ConsensusBootstrapInfo info;
  info.last_id.set_term(10);
  for (int i = 0; i < 10; i++) {
    auto replicate = new ReplicateMsg();
    replicate->set_op_type(NO_OP);
    info.last_id.set_index(100 + i);
    replicate->mutable_id()->CopyFrom(info.last_id);
    info.orphaned_replicates.push_back(replicate);
  }

  info.last_committed_id.set_term(10);
  info.last_committed_id.set_index(104);

  {
    InSequence dummy;
    // On start we expect 10 NO_OPs to be enqueues, with 5 of those having
    // their commit continuation called immediately.
    EXPECT_CALL(*consensus_.get(), StartConsensusOnlyRoundUnlocked(_))
        .Times(10);

    // Queue gets initted when the peer starts.
    EXPECT_CALL(*queue_, Init(_))
      .Times(1);
  }

  ASSERT_OK(consensus_->Start(info));

  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(queue_));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(txn_factory_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(peer_manager_));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(consensus_.get()));

  // Now we test what this peer does with the pending operations once it's elected leader.
  {
    InSequence dummy;
    // Peer manager gets updated with the new set of peers to send stuff to.
    EXPECT_CALL(*peer_manager_, UpdateRaftConfig(_))
        .Times(1).WillOnce(Return(Status::OK()));
    // The no-op should be appended to the queue.
    // One more op will be appended for the election.
    EXPECT_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
        .Times(1);
    EXPECT_CALL(*queue_, AppendOperationsMock(_, _))
        .Times(1).WillRepeatedly(Return(Status::OK()));;
  }

  // Emulate an election, this will make this peer become leader and trigger the
  // above set expectations.
  ASSERT_OK(consensus_->EmulateElection());

  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(queue_));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(txn_factory_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(peer_manager_));

  // Commit the 5 no-ops from the previous term, along with the one pushed to
  // assert leadership.
  EXPECT_CALL(*consensus_.get(), NonTxRoundReplicationFinished(HasOpId(), _, IsOk()))
      .Times(6);
  EXPECT_CALL(*peer_manager_, SignalRequest(_))
      .Times(AnyNumber());
  // In the end peer manager and the queue get closed.
  EXPECT_CALL(*peer_manager_, Close())
      .Times(AtLeast(1));
  EXPECT_CALL(*queue_, Close())
      .Times(1);

  // Now tell consensus all original orphaned replicates were majority replicated.
  // This should not advance the committed index because we haven't replicated
  // anything in the current term.
  OpId committed_index;
  consensus_->UpdateMajorityReplicated(info.orphaned_replicates.back()->id(),
                                       &committed_index);
  // Should still be the last committed in the the wal.
  ASSERT_OPID_EQ(committed_index, info.last_committed_id);

  // Now mark the last operation (the no-op round) as committed.
  // This should advance the committed index, since that round in on our current term,
  // and we should be able to commit all previous rounds.
  OpId cc_round_id = info.orphaned_replicates.back()->id();
  cc_round_id.set_term(11);
  cc_round_id.set_index(cc_round_id.index() + 1);
  consensus_->UpdateMajorityReplicated(cc_round_id,
                                       &committed_index);

  ASSERT_OPID_EQ(committed_index, cc_round_id);
}

MATCHER_P2(RoundHasOpId, term, index, "") {
  LOG(INFO) << "expected: " << MakeOpId(term, index) << ", actual: " << arg->id();
  return arg->id().term() == term && arg->id().index() == index;
}

// Tests the case where a a leader is elected and pushed a sequence of
// operations of which some never get committed. Eventually a new leader in a higher
// term pushes operations that overwrite some of the original indexes.
TEST_F(RaftConsensusTest, TestAbortOperations) {
  SetUpConsensus(1, 2);

  EXPECT_CALL(*consensus_.get(), AppendNewRoundToQueueUnlocked(_))
      .Times(AnyNumber());

  EXPECT_CALL(*peer_manager_, SignalRequest(_))
      .Times(AnyNumber());
  EXPECT_CALL(*peer_manager_, Close())
      .Times(AtLeast(1));
  EXPECT_CALL(*queue_, Close())
      .Times(1);
  EXPECT_CALL(*queue_, Init(_))
      .Times(1);
  EXPECT_CALL(*peer_manager_, UpdateRaftConfig(_))
      .Times(1)
      .WillRepeatedly(Return(Status::OK()));

  // We'll append to the queue 12 times, the initial noop txn + 10 initial ops while leader
  // and the new leader's update, when we're overwriting operations.
  EXPECT_CALL(*queue_, AppendOperationsMock(_, _))
      .Times(12);

  // .. but those will be overwritten later by another
  // leader, which will push and commit 5 ops.
  // Only these five should start as replica rounds.
  EXPECT_CALL(*consensus_.get(), StartConsensusOnlyRoundUnlocked(_))
      .Times(4);

  ConsensusBootstrapInfo info;
  ASSERT_OK(consensus_->Start(info));
  ASSERT_OK(consensus_->EmulateElection());

  // Append 10 rounds: 2.2 - 2.11
  for (int i = 0; i < 10; i++) {
    AppendNoOpRound();
  }

  // Expectations for what gets committed and what gets aborted:
  // (note: the aborts may be triggered before the commits)
  // 5 OK's for the 2.1-2.5 ops.
  // 6 Aborts for the 2.6-2.11 ops.
  // 1 OK for the 3.6 op.
  for (int index = 1; index < 6; index++) {
    EXPECT_CALL(*consensus_.get(),
                NonTxRoundReplicationFinished(RoundHasOpId(2, index), _, IsOk())).Times(1);
  }
  for (int index = 6; index < 12; index++) {
    EXPECT_CALL(*consensus_.get(),
                NonTxRoundReplicationFinished(RoundHasOpId(2, index), _, IsAborted())).Times(1);
  }
  EXPECT_CALL(*consensus_.get(),
              NonTxRoundReplicationFinished(RoundHasOpId(3, 6), _, IsOk())).Times(1);

  // Nothing's committed so far, so now just send an Update() message
  // emulating another guy got elected leader and is overwriting a suffix
  // of the previous messages.
  // In particular this request has:
  // - Op 2.5 from the previous leader's term
  // - Ops 3.6-3.9 from the new leader's term
  // - A new committed index of 3.6
  ConsensusRequestPB request;
  request.set_caller_term(3);
  const string PEER_0_UUID = "peer-0";
  request.set_caller_uuid(PEER_0_UUID);
  request.set_tablet_id(kTestTablet);
  request.mutable_preceding_id()->CopyFrom(MakeOpId(2, 4));

  ReplicateMsg* replicate = request.add_ops();
  replicate->mutable_id()->CopyFrom(MakeOpId(2, 5));
  replicate->set_op_type(NO_OP);

  ReplicateMsg* noop_msg = request.add_ops();
  noop_msg->mutable_id()->CopyFrom(MakeOpId(3, 6));
  noop_msg->set_op_type(NO_OP);
  noop_msg->set_timestamp(clock_->Now().ToUint64());
  noop_msg->mutable_noop_request();

  // Overwrite another 3 of the original rounds for a total of 4 overwrites.
  for (int i = 7; i < 10; i++) {
    ReplicateMsg* replicate = request.add_ops();
    replicate->mutable_id()->CopyFrom(MakeOpId(3, i));
    replicate->set_op_type(NO_OP);
    replicate->set_timestamp(clock_->Now().ToUint64());
  }

  request.mutable_committed_index()->CopyFrom(MakeOpId(3, 6));

  ConsensusResponsePB response;
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.has_error());

  ASSERT_TRUE(Mock::VerifyAndClearExpectations(consensus_.get()));

  // Now we expect to commit ops 3.7 - 3.9.
  for (int index = 7; index < 10; index++) {
    EXPECT_CALL(*consensus_.get(),
                NonTxRoundReplicationFinished(RoundHasOpId(3, index), _, IsOk())).Times(1);
  }

  request.mutable_ops()->Clear();
  request.mutable_preceding_id()->CopyFrom(MakeOpId(3, 9));
  request.mutable_committed_index()->CopyFrom(MakeOpId(3, 9));

  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.has_error());
}

TEST_F(RaftConsensusTest, TestReceivedIdIsInittedBeforeStart) {
  SetUpConsensus();
  OpId opid;
  ASSERT_OK(consensus_->GetLastOpId(RECEIVED_OPID, &opid));
  ASSERT_TRUE(opid.IsInitialized());
  ASSERT_OPID_EQ(opid, MinimumOpId());
}

// Ensure that followers reset their "last_received_current_leader"
// ConsensusStatusPB field when a new term is encountered. This is a
// correctness test for the logic on the follower side that allows the
// leader-side queue to determine which op to send next in various scenarios.
TEST_F(RaftConsensusTest, TestResetRcvdFromCurrentLeaderOnNewTerm) {
  SetUpConsensus(kMinimumTerm, 3);
  SetUpGeneralExpectations();
  ConsensusBootstrapInfo info;
  ASSERT_OK(consensus_->Start(info));

  ConsensusRequestPB request;
  ConsensusResponsePB response;
  int64_t caller_term = 0;
  int64_t log_index = 0;

  caller_term = 1;
  string caller_uuid = config_.peers(0).permanent_uuid();
  OpId preceding_opid = MinimumOpId();

  // Heartbeat. This will cause the term to increment on the follower.
  request = MakeConsensusRequest(caller_term, caller_uuid, preceding_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_EQ(caller_term, response.responder_term());
  ASSERT_OPID_EQ(response.status().last_received(), MinimumOpId());
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), MinimumOpId());

  // Replicate a no-op.
  OpId noop_opid = MakeOpId(caller_term, ++log_index);
  AddNoOpToConsensusRequest(&request, noop_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_OPID_EQ(response.status().last_received(), noop_opid);
  ASSERT_OPID_EQ(response.status().last_received_current_leader(),  noop_opid);

  // New leader heartbeat. Term increase to 2.
  // Expect current term replicated to be nothing (MinimumOpId) but log
  // replicated to be everything sent so far.
  caller_term = 2;
  caller_uuid = config_.peers(1).permanent_uuid();
  preceding_opid = noop_opid;
  request = MakeConsensusRequest(caller_term, caller_uuid, preceding_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_EQ(caller_term, response.responder_term());
  ASSERT_OPID_EQ(response.status().last_received(), preceding_opid);
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), MinimumOpId());

  // Append a no-op.
  noop_opid = MakeOpId(caller_term, ++log_index);
  AddNoOpToConsensusRequest(&request, noop_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_OPID_EQ(response.status().last_received(), noop_opid);
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), noop_opid);

  // New leader heartbeat. The term should rev but we should get an LMP mismatch.
  caller_term = 3;
  caller_uuid = config_.peers(0).permanent_uuid();
  preceding_opid = MakeOpId(caller_term, log_index + 1); // Not replicated yet.
  request = MakeConsensusRequest(caller_term, caller_uuid, preceding_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_EQ(caller_term, response.responder_term());
  ASSERT_OPID_EQ(response.status().last_received(), noop_opid); // Not preceding this time.
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), MinimumOpId());
  ASSERT_TRUE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_EQ(ConsensusErrorPB::PRECEDING_ENTRY_DIDNT_MATCH, response.status().error().code());

  // Decrement preceding and append a no-op.
  preceding_opid = MakeOpId(2, log_index);
  noop_opid = MakeOpId(caller_term, ++log_index);
  request = MakeConsensusRequest(caller_term, caller_uuid, preceding_opid);
  AddNoOpToConsensusRequest(&request, noop_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_OPID_EQ(response.status().last_received(), noop_opid) << response.ShortDebugString();
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), noop_opid)
      << response.ShortDebugString();

  // Happy case. New leader with new no-op to append right off the bat.
  // Response should be OK with all last_received* fields equal to the new no-op.
  caller_term = 4;
  caller_uuid = config_.peers(1).permanent_uuid();
  preceding_opid = noop_opid;
  noop_opid = MakeOpId(caller_term, ++log_index);
  request = MakeConsensusRequest(caller_term, caller_uuid, preceding_opid);
  AddNoOpToConsensusRequest(&request, noop_opid);
  response.Clear();
  ASSERT_OK(consensus_->Update(&request, &response));
  ASSERT_FALSE(response.status().has_error()) << response.ShortDebugString();
  ASSERT_EQ(caller_term, response.responder_term());
  ASSERT_OPID_EQ(response.status().last_received(), noop_opid);
  ASSERT_OPID_EQ(response.status().last_received_current_leader(), noop_opid);
}

}  // namespace consensus
}  // namespace kudu
