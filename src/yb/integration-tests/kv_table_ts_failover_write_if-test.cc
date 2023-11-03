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

#include <atomic>
#include <memory>
#include <vector>

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/session.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"

#include "yb/yql/cql/ql/util/statement_result.h"

DECLARE_bool(TEST_combine_batcher_errors);

namespace yb {

using client::YBSessionPtr;
using client::YBSchemaBuilder;
using client::YBqlWriteOp;
using itest::TServerDetails;
using std::shared_ptr;
using std::vector;
using std::string;

using namespace std::literals;

namespace {

const auto kTestCommandsTimeOut = 30s;
const auto kHeartBeatInterval = 1s;
const auto kLeaderFailureMaxMissedHeartbeatPeriods = 3;
const auto kKeyColumnName = "k";
const auto kValueColumnName = "v";

std::string TsNameForIndex(size_t idx) {
  return Format("ts-$0", idx + 1);
}

} // namespace

class KVTableTsFailoverWriteIfTest : public integration_tests::YBTableTestBase {
 public:
  bool use_external_mini_cluster() override { return true; }

  int num_tablets() override { return 1; }

  bool enable_ysql() override { return false; }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--raft_heartbeat_interval_ms=" +
        yb::ToString(ToMilliseconds(kHeartBeatInterval)));
    opts->extra_tserver_flags.push_back("--leader_failure_max_missed_heartbeat_periods=" +
        yb::ToString(kLeaderFailureMaxMissedHeartbeatPeriods));
  }

  void SetUp() override {
    YBTableTestBase::SetUp();
    ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(external_mini_cluster()));
    ts_details_.clear();
    for (size_t i = 0; i < external_mini_cluster()->num_tablet_servers(); ++i) {
      std::string ts_id = external_mini_cluster()->tablet_server(i)->uuid();
      LOG(INFO) << TsNameForIndex(i) << ": " << ts_id;
      TServerDetails* ts = ts_map_[ts_id].get();
      ASSERT_EQ(ts->uuid(), ts_id);
      ts_details_.push_back(ts);
    }
  }

  void SetValueAsync(const YBSessionPtr& session, int32_t key, int32_t value) {
    const auto insert = table_.NewInsertOp();
    auto* const req = insert->mutable_request();
    QLAddInt32HashValue(req, key);
    table_.AddInt32ColumnValue(req, kValueColumnName, value);
    string op_str = Format("$0: $1", key, value);
    LOG(INFO) << "Sending write: " << op_str;
    session->Apply(insert);
    session->FlushAsync([insert, op_str](client::FlushStatus* flush_status) {
      const auto& s = flush_status->status;
      ASSERT_TRUE(s.ok() || s.IsAlreadyPresent())
          << "Failed to flush write " << op_str << ". Error: " << s;
      if (s.ok()) {
        ASSERT_EQ(insert->response().status(), QLResponsePB::YQL_STATUS_OK)
            << "Failed to write " << op_str;
      }
      LOG(INFO) << "Written: " << op_str;
    });
  }

  shared_ptr<YBqlWriteOp> CreateWriteIfOp(int32_t key, int32_t old_value, int32_t new_value) {
    const auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
    auto* const req = op->mutable_request();
    // Set v = new_value.
    table_.AddInt32ColumnValue(req, kValueColumnName, new_value);
    // If v = old_value.
    table_.SetInt32Condition(
        req->mutable_if_expr()->mutable_condition(), kValueColumnName, QL_OP_EQUAL, old_value);
    req->mutable_column_refs()->add_ids(table_.ColumnId(kValueColumnName));
    // And k = key.
    QLAddInt32HashValue(req, key);
    return op;
  }

  boost::optional<int32_t> GetValue(const YBSessionPtr& session, int32_t key) {
    const auto op = client::CreateReadOp(key, table_, kValueColumnName);
    Status s = session->TEST_ApplyAndFlush(op);
    if (!s.ok()) {
      return boost::none;
    }
    auto rowblock = ql::RowsResult(op.get()).GetRowBlock();
    EXPECT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
    if (rowblock->row_count() == 0) {
      return boost::none;
    }
    EXPECT_EQ(1, rowblock->row_count());
    return rowblock->row(0).column(0).int32_value();
  }

  void CreateTable() override {
    if (!table_exists_) {
      const auto table = table_name();
      ASSERT_OK(client_->CreateNamespaceIfNotExists(table.namespace_name(),
                                                    table.namespace_type()));

      YBSchemaBuilder b;
      b.AddColumn(kKeyColumnName)->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
      b.AddColumn(kValueColumnName)->Type(DataType::INT32)->NotNull();
      ASSERT_OK(b.Build(&schema_));

      ASSERT_OK(NewTableCreator()->table_name(table_name()).schema(&schema_).Create());
      table_exists_ = true;
    }
  }

  Result<int> GetTabletServerLeaderIndex(const std::string& tablet_id) {
    TServerDetails* leader_ts_details;
    Status s = FindTabletLeader(ts_map_, tablet_id, kTestCommandsTimeOut, &leader_ts_details);
    RETURN_NOT_OK(s);
    const int idx = external_mini_cluster()->tablet_server_index_by_uuid(leader_ts_details->uuid());
    if (idx < 0) {
      return STATUS_FORMAT(
          IllegalState, "Not found tablet server with uuid: $0", leader_ts_details->uuid());
    }
    LOG(INFO) << "Tablet server leader detected: " << TsNameForIndex(idx);
    return idx;
  }

  Result<int> GetTabletServerRaftLeaderIndex(const std::string& tablet_id) {
    const MonoDelta kMinTimeout = 1s;
    MonoTime start = MonoTime::Now();
    MonoTime deadline = start + 60s;
    Status s;
    int i = 0;
    while (true) {
      MonoDelta remaining_timeout = deadline - MonoTime::Now();
      TServerDetails* ts = ts_details_[i];
      consensus::ConsensusStatePB cstate;
      s = itest::GetConsensusState(ts,
                                  tablet_id,
                                  consensus::ConsensusConfigType::CONSENSUS_CONFIG_ACTIVE,
                                  std::min(remaining_timeout, kMinTimeout),
                                  &cstate);
      if (s.ok() && cstate.has_leader_uuid() && cstate.leader_uuid() == ts->uuid()) {
       LOG(INFO) << "Tablet server RAFT leader detected: " << TsNameForIndex(i);
       return i;
      }
      if (MonoTime::Now() > deadline) {
       break;
      }
      SleepFor(100ms);
      i = (i + 1) % ts_details_.size();
    }
    return STATUS(NotFound, "No tablet server RAFT leader detected");
  }

  void SetBoolFlag(size_t ts_idx, const std::string& flag, bool value) {
    auto ts = external_mini_cluster()->tablet_server(ts_idx);
    LOG(INFO) << "Setting " << flag << " to " << value << " on " << TsNameForIndex(ts_idx);
    ASSERT_OK(external_mini_cluster()->SetFlag(ts, flag, yb::ToString(value)));
  }

  itest::TabletServerMap ts_map_;

  // TServerDetails instances referred by ts_details_ are owned by ts_map_.
  vector<TServerDetails*> ts_details_;
};

// Test for ENG-3471 - shouldn't run write-if when leader hasn't yet committed all pendings ops.
TEST_F(KVTableTsFailoverWriteIfTest, KillTabletServerDuringReplication) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_combine_batcher_errors) = true;

  const int32_t key = 0;
  const int32_t initial_value = 10000;
  const auto small_delay = 100ms;

  const auto cluster = external_mini_cluster();
  const auto num_ts = cluster->num_tablet_servers();

  vector<string> tablet_ids;
  do {
    ASSERT_OK(itest::ListRunningTabletIds(
        ts_map_.begin()->second.get(), kTestCommandsTimeOut, &tablet_ids));
  } while (tablet_ids.empty());
  const auto tablet_id = tablet_ids[0];

  const auto old_leader_ts_idx = ASSERT_RESULT(GetTabletServerLeaderIndex(tablet_id));
  auto* const old_leader_ts = cluster->tablet_server(old_leader_ts_idx);

  // Select replica to be a new leader.
  const auto new_leader_ts_idx = (old_leader_ts_idx + 1) % num_ts;
  const auto new_leader_name = TsNameForIndex(new_leader_ts_idx);
  auto* const new_leader_ts = cluster->tablet_server(new_leader_ts_idx);

  // Select replica to be follower all the time.
  const auto follower_replica_ts_idx = (new_leader_ts_idx + 1) % num_ts;

  SetValueAsync(session_, key, initial_value);

  // Run concurrent reads.
  std::atomic<bool> stop_requested(false);
  std::atomic<int32_t> last_read_value(0);
  std::thread reader([this, &stop_requested, &small_delay, &last_read_value] {
    auto session = NewSession();
    MonoTime log_deadline(MonoTime::Min());

    while (!stop_requested) {
      SleepFor(small_delay);
      auto val = GetValue(session, key);
      if (!val) {
        continue;
      }
      const auto last_value = last_read_value.exchange(*val);
      if (*val != last_value || MonoTime::Now() > log_deadline) {
        LOG(INFO) << "Read value: " << key << ": " << *val;
        log_deadline = MonoTime::Now() + 1s;
      }
      ASSERT_GE(*val, last_value);
    }
  });

  // Make sure we read initial value.
  ASSERT_OK(LoggedWaitFor(
      [&last_read_value]{ return last_read_value == initial_value; }, 60s,
      "Waiting to read initial value...", small_delay));

  // Prevent follower_replica_ts_idx from being elected as a new leader.
  SetBoolFlag(follower_replica_ts_idx, "TEST_follower_reject_update_consensus_requests", true);

  {
    LogWaiter log_waiter(new_leader_ts, "Pausing due to flag TEST_pause_update_replica");

    // Pause Consensus Update RPC processing on new_leader_ts to delay initial_value + 2 replication
    // till new_leader_ts is elected as a new leader.
    SetBoolFlag(new_leader_ts_idx, "TEST_pause_update_replica", true);

    // Send write initial_value + 2, it won't be fully replicated until we resume UpdateReplica and
    // UpdateMajorityReplicated.
    SetValueAsync(session_, key, initial_value + 2);

    LOG(INFO) << Format("Waiting for Consensus Update RPC to arrive $0...", new_leader_name);
    ASSERT_OK(log_waiter.WaitFor(60s));
  }

  {
    LogWaiter log_waiter(new_leader_ts, "Starting NORMAL_ELECTION...");

    // Pausing leader and waiting, so replicas will detect leader failure.
    LOG(INFO) << "Pausing " << TsNameForIndex(old_leader_ts_idx);
    ASSERT_OK(old_leader_ts->Pause());

    LOG(INFO) << Format("Waiting for election to start on $0...", new_leader_name);
    ASSERT_OK(log_waiter.WaitFor(60s));
  }

  {
    LogWaiter log_waiter(
        new_leader_ts, "Pausing due to flag TEST_pause_update_majority_replicated");

    // Pause applying write ops on new_leader_ts_idx, so initial_value + 2 won't be applied to DocDB
    // yet after going to RAFT log.
    SetBoolFlag(new_leader_ts_idx, "TEST_pause_update_majority_replicated", true);

    // Resume UpdateReplica on new_leader_ts_idx, so it can:
    // 1 - Trigger election (RaftConsensus::ReportFailureDetectedTask is waiting on lock inside
    // LOG_WITH_PREFIX).
    // 2 - Append initial_value + 2 to RAFT log.
    SetBoolFlag(new_leader_ts_idx, "TEST_pause_update_replica", false);

    // new_leader_ts_idx will become leader, but might not be ready to serve due to pending write
    // ops to apply.
    {
      const auto leader_idx = ASSERT_RESULT(GetTabletServerRaftLeaderIndex(tablet_id));
      ASSERT_EQ(leader_idx, new_leader_ts_idx);
    }

    // We need follower_replica_ts_idx to be able to process Consensus Update RPC from new leader.
    SetBoolFlag(follower_replica_ts_idx, "TEST_follower_reject_update_consensus_requests", false);

    LOG(INFO) << Format(
        "Waiting for $0 to append to RAFT log on $1...", initial_value + 2, new_leader_name);
    ASSERT_OK(log_waiter.WaitFor(60s));
  }

  // Make following CAS to pause after doing read and evaluating if part.
  SetBoolFlag(new_leader_ts_idx, "TEST_pause_write_apply_after_if", true);

  // Send CAS initial_value -> initial_value + 1.
  std::atomic<bool> cas_completed(false);
  const auto cas_old_value = initial_value;
  const auto cas_new_value = initial_value + 1;
  const auto op = CreateWriteIfOp(key, cas_old_value, cas_new_value);
  const auto op_str = Format("$0: $1 -> $2", key, cas_old_value, cas_new_value);
  LOG(INFO) << "Sending CAS " << op_str;
  auto session = NewSession();
  session->SetTimeout(15s);
  client::FlushCallback callback = [&session, &op, &op_str, &cas_completed,
                                    &callback](client::FlushStatus* flush_status) {
    const auto& s = flush_status->status;
    if (s.ok()) {
      LOG(INFO) << "CAS operation completed: " << op_str;
      cas_completed.store(true);
    } else {
      LOG(INFO) << "Error during CAS: " << s;
      LOG(INFO) << "Retrying CAS: " << op_str;
      session->Apply(op);
      session->FlushAsync(callback);
    }
  };
  session->Apply(op);
  session->FlushAsync(callback);

  // In case of bug ENG-3471 read part of CAS will be completed before appending pending ops to log,
  // so give it some time to CAS-read and prepare to put write (initial_value + 1) to RAFT log.
  SleepFor(3s);

  // Let write (initial_value + 2) to be applied to DocDB.
  SetBoolFlag(new_leader_ts_idx, "TEST_pause_update_majority_replicated", false);

  // Waiting to read (initial_value + 2) from new leader.
  {
    const auto desc = Format("Waiting to read $0...", initial_value + 2);
    LOG(INFO) << desc;
    ASSERT_OK(WaitFor(
        [&last_read_value]{ return last_read_value == initial_value + 2; }, 60s, desc, small_delay,
        1));
  }

  // Resume CAS processing.
  SetBoolFlag(new_leader_ts_idx, "TEST_pause_write_apply_after_if", false);

  {
    const auto desc = "Waiting for CAS to complete...";
    LOG(INFO) << desc;
    ASSERT_OK(WaitFor([&cas_completed]{ return cas_completed.load(); }, 60s, desc, small_delay, 1));
  }

  // Give reader thread some time to read CAS result in case CAS condition was met.
  SleepFor(3s);

  stop_requested.store(true);
  LOG(INFO) << "Waiting for reader thread...";
  reader.join();
  LOG(INFO) << "Reader thread stopped.";

  LOG(INFO) << "Resuming " << TsNameForIndex(old_leader_ts_idx);
  ASSERT_OK(old_leader_ts->Resume());

  ClusterVerifier cluster_verifier(external_mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
}

}  // namespace yb
