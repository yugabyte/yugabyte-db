// Copyright (c) YugabyteDB, Inc.
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

#include <string>
#include <utility>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/primitive_value.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver.messages.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/memory/arena.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

using namespace std::chrono_literals;

DECLARE_bool(enable_load_balancing);

namespace yb {

using dockv::DocKey;
using dockv::MakeKeyEntryValues;

namespace {

const client::YBTableName kTableName(
    YQL_DATABASE_CQL, "my_keyspace", "fixed_ht_write_id_test");

}  // namespace

// Multi-replica coverage for fixed-hybrid-time writes that derive their DocDB write ID from the
// Raft operation index (KeyValueWriteBatchPB.use_raft_index_for_write_id). The single-peer spike
// tests prove storage semantics and WAL-replay determinism; this test proves the two remaining
// replication-level properties:
//   1. Follower apply determinism: every replica applies a marked batch with the same
//      OpId-derived write ID, producing byte-identical DocDB regular-database contents.
//   2. Promotion continuity: after a leader change, marked writes on the new leader keep drawing
//      write IDs from the (monotonically continuing) Raft index sequence, so a same-key write at
//      the same fixed hybrid time lands as a second, distinct physical version instead of
//      colliding with the version written under the old leader.
class FixedHybridTimeWriteIdITest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    // The base class enables pretty (local-time) hybrid-time rendering, which is
    // timezone-dependent. This test compares DocDB dump strings against expected literals, so
    // switch back to the deterministic "physical: N" rendering.
    HybridTime::TEST_SetPrettyToString(false);

    // Keep tablet leadership where the test puts it.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.num_masters = 1;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    client_ = ASSERT_RESULT(cluster_->CreateClient());

    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        kTableName.namespace_name(), kTableName.namespace_type()));
    client::YBSchemaBuilder builder;
    builder.AddColumn("k")->Type(DataType::STRING)->HashPrimaryKey()->NotNull();
    builder.AddColumn("v")->Type(DataType::STRING)->Nullable();
    ASSERT_OK(table_.Create(kTableName, /* num_tablets= */ 1, client_.get(), &builder));
  }

  void DoTearDown() override {
    client_.reset();
    YBMiniClusterTestBase::DoTearDown();  // Shuts down and resets cluster_.
  }

  // One marked write entry: (hash code, range key, value).
  using MarkedWriteEntry = std::tuple<uint16_t, std::string, std::string>;

  // Sends one marked (use_raft_index_for_write_id) non-transactional write batch at the given
  // fixed hybrid time through the current leader's Write RPC, so it takes the production
  // validation -> Raft -> WAL -> apply path on every replica. Each entry is a column-level write
  // (DocKey + ColumnId("v") -> value), the same shape a regular YCQL column write produces, so
  // scans of the table (e.g. the teardown cluster-verification checksum) decode it. Returns the
  // leader's last committed OpId right after the write, which -- on this otherwise idle
  // single-tablet Raft group -- is the write's own OpId.
  Result<OpId> SendMarkedWriteEntries(
      const TabletId& tablet_id, HybridTime write_ht,
      const std::vector<MarkedWriteEntry>& entries) {
    size_t leader_idx = 0;
    auto* leader_ts = GetLeaderForTablet(cluster_.get(), tablet_id, &leader_idx);
    SCHECK_NOTNULL(leader_ts);
    auto proxy = leader_ts->server()->proxy();

    tserver::WriteRequestPB req;
    req.set_tablet_id(tablet_id);
    req.set_external_hybrid_time(write_ht.ToUint64());
    auto* write_batch = req.mutable_write_batch();
    write_batch->set_use_raft_index_for_write_id(true);
    const yb::ColumnId value_column_id(table_.ColumnId("v"));
    for (const auto& [hash, key, value] : entries) {
      const DocKey doc_key(hash, MakeKeyEntryValues(key));
      dockv::KeyBytes column_key = doc_key.Encode();
      dockv::KeyEntryValue::MakeColumnId(value_column_id).AppendToKey(&column_key);
      auto* write_pair = write_batch->add_write_pairs();
      write_pair->set_key(column_key.AsSlice().cdata(), column_key.AsSlice().size());
      std::string encoded_value;
      dockv::AppendEncodedValue(QLValue::Primitive(value), &encoded_value);
      write_pair->set_value(encoded_value);
    }

    RETURN_NOT_OK(LoggedWaitFor(
        [&req, &proxy]() -> Result<bool> {
          auto arena = SharedThreadSafeArena();
          auto lw_req = arena->NewArenaObject<tserver::LWWriteRequestPB>(req);
          auto lw_resp = arena->NewArenaObject<tserver::LWWriteResponsePB>();
          rpc::RpcController rpc;
          auto s = proxy->Write(*lw_req, lw_resp, &rpc);
          if (s.IsTryAgain()) {
            return false;
          }
          RETURN_NOT_OK(s);
          tserver::WriteResponsePB resp;
          lw_resp->ToGoogleProtobuf(&resp);
          if (resp.has_error()) {
            auto status = StatusFromPB(resp.error().status());
            if (status.IsTryAgain()) {
              return false;
            }
            return status;
          }
          return true;
        },
        MonoDelta::FromSeconds(10) * kTimeMultiplier, "marked write RPC to be accepted"));

    auto leader_peer = VERIFY_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
    return VERIFY_RESULT(leader_peer->GetConsensus())->GetLastCommittedOpId();
  }

  // Hash-0 convenience wrapper: dump-literal comparisons do not re-derive hashes, and the single
  // tablet covers the entire hash range.
  Result<OpId> SendMarkedWrite(
      const TabletId& tablet_id, HybridTime write_ht,
      const std::vector<std::pair<std::string, std::string>>& kvs) {
    std::vector<MarkedWriteEntry> entries;
    entries.reserve(kvs.size());
    for (const auto& [key, value] : kvs) {
      entries.emplace_back(0, key, value);
    }
    return SendMarkedWriteEntries(tablet_id, write_ht, entries);
  }

  // Waits until every replica of the tablet has applied at least up to op_id, so DocDB dumps
  // observe the write on followers, not only on the leader.
  Status WaitAllReplicasApplied(const TabletId& tablet_id, const OpId& op_id) {
    auto peers = VERIFY_RESULT(ListTabletPeers(cluster_.get(), tablet_id));
    SCHECK_EQ(peers.size(), 3U, IllegalState, "Expected all three replicas to be running");
    for (const auto& peer : peers) {
      RETURN_NOT_OK(LoggedWaitFor(
          [&peer, &op_id]() -> Result<bool> {
            return VERIFY_RESULT(peer->GetConsensus())->GetLastAppliedOpId() >= op_id;
          },
          MonoDelta::FromSeconds(15) * kTimeMultiplier,
          Format("replica $0 to apply $1", peer->permanent_uuid(), op_id)));
    }
    return Status::OK();
  }

  // Renders the expected DocDB dump line for one column-level entry written by SendMarkedWrite
  // at kWriteHT under the given Raft operation index. The stored write ID is the index lifted
  // into the reserved marked domain: kBackfillWriteIdFloor | index.
  std::string ExpectedEntry(const std::string& key, int64_t raft_index, const std::string& value) {
    return Format(
        "SubDocKey(DocKey(0x0000, [\"$0\"], []), [ColumnId($1); HT{ physical: $2 w: $3 }]) "
        "-> \"$4\"",
        key, table_.ColumnId("v"), kWriteHT.GetPhysicalValueMicros(),
        kBackfillWriteIdFloor | static_cast<IntraTxnWriteId>(raft_index), value);
  }

  // Asserts that the DocDB regular database of every replica contains exactly the expected
  // entries: follower apply must be byte-identical to the leader, write IDs included.
  void AssertAllReplicasDumpTo(
      const TabletId& tablet_id, const std::vector<std::string>& expected) {
    auto peers = ASSERT_RESULT(ListTabletPeers(cluster_.get(), tablet_id));
    ASSERT_EQ(peers.size(), 3U);
    for (const auto& peer : peers) {
      auto tablet = ASSERT_RESULT(peer->shared_tablet());
      std::vector<std::string> entries;
      tablet->TEST_DocDBDumpToContainer(entries, docdb::IncludeIntents::kFalse);
      ASSERT_EQ(expected, entries) << "Unexpected DocDB contents on replica "
                                   << peer->permanent_uuid();
    }
  }

  // Fixed (arbitrary, past) hybrid time shared by every marked write in the test.
  static constexpr HybridTime kWriteHT = 1000_usec_ht;

  std::unique_ptr<client::YBClient> client_;
  client::TableHandle table_;
};

TEST_F(FixedHybridTimeWriteIdITest, LeaderChangePreservesDistinctWriteIds) {
  auto leaders = ASSERT_RESULT(WaitForTableActiveTabletLeadersPeers(
      cluster_.get(), table_.table()->id(), /* num_active_leaders= */ 1));
  const auto tablet_id = leaders.front()->tablet_id();
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id, CoarseMonoClock::Now() + 10s * kTimeMultiplier,
      RequireLeaderIsReady::kTrue));

  // W1 under the original leader: two distinct keys in one marked op share the op's Raft-index
  // write ID ("dup" is the key W2 collides with later; "stable" pins the shared-ID property).
  const auto w1_op_id = ASSERT_RESULT(SendMarkedWrite(
      tablet_id, kWriteHT, {{"dup", "first"}, {"stable", "before"}}));
  ASSERT_GT(w1_op_id.index, 0);
  ASSERT_OK(WaitAllReplicasApplied(tablet_id, w1_op_id));

  const std::vector<std::string> expected_after_w1 = {
      ExpectedEntry("dup", w1_op_id.index, "first"),
      ExpectedEntry("stable", w1_op_id.index, "before")};
  ASSERT_NO_FATALS(AssertAllReplicasDumpTo(tablet_id, expected_after_w1));

  // Promote a follower that has already applied W1.
  auto old_leader = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  const auto old_leader_uuid = old_leader->permanent_uuid();
  std::string new_leader_uuid;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeers(cluster_.get(), tablet_id))) {
    if (peer->permanent_uuid() != old_leader_uuid) {
      new_leader_uuid = peer->permanent_uuid();
      break;
    }
  }
  ASSERT_FALSE(new_leader_uuid.empty());
  ASSERT_OK(TransferLeadership(
      cluster_.get(), tablet_id, new_leader_uuid, MonoDelta::FromSeconds(30) * kTimeMultiplier));
  ASSERT_OK(WaitUntilTabletHasLeader(
      cluster_.get(), tablet_id, CoarseMonoClock::Now() + 30s * kTimeMultiplier,
      RequireLeaderIsReady::kTrue));
  auto new_leader = ASSERT_RESULT(GetLeaderPeerForTablet(cluster_.get(), tablet_id));
  ASSERT_EQ(new_leader->permanent_uuid(), new_leader_uuid);

  // W2 under the new leader: the same key at the same fixed hybrid time. With positional write
  // IDs this would produce a byte-identical DocDB key and silently shadow W1's version; the
  // Raft-index write ID must keep both.
  const auto w2_op_id = ASSERT_RESULT(SendMarkedWrite(tablet_id, kWriteHT, {{"dup", "second"}}));
  // A real term change happened, and the Raft index continued monotonically across it: the new
  // leader's marked write cannot reuse a write ID issued under the old leader.
  ASSERT_GT(w2_op_id.term, w1_op_id.term);
  ASSERT_GT(w2_op_id.index, w1_op_id.index);
  ASSERT_OK(WaitAllReplicasApplied(tablet_id, w2_op_id));

  // Every replica -- including the demoted old leader, which applies W2 as a follower -- must
  // hold both physical versions of "dup" at the same hybrid time under distinct write IDs
  // (newest write ID first within the key), plus the untouched "stable" entry.
  const std::vector<std::string> expected_after_w2 = {
      ExpectedEntry("dup", w2_op_id.index, "second"),
      ExpectedEntry("dup", w1_op_id.index, "first"),
      ExpectedEntry("stable", w1_op_id.index, "before")};
  ASSERT_NO_FATALS(AssertAllReplicasDumpTo(tablet_id, expected_after_w2));
}

}  // namespace yb
