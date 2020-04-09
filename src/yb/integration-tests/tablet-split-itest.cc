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

#include "yb/client/client-test-util.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"

#include "yb/consensus/consensus.h"

#include "yb/master/catalog_manager.h"

#include "yb/rpc/messenger.h"

#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/size_literals.h"
#include "yb/util/test_util.h"

using namespace std::literals;  // NOLINT

DECLARE_int64(db_write_buffer_size);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_bool(do_not_start_election_test_only);

namespace yb {

class TabletSplitITest : public client::KeyValueTableTest {
 public:
  void SetUp() override {
    mini_cluster_opt_.num_tablet_servers = 3;
    client::KeyValueTableTest::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  }

  // Creates read request for tablet_id which reflects following query (see
  // client::KeyValueTableTest for schema and kXxx constants):
  // SELECT `kValueColumn` FROM `kTableName` WHERE `kKeyColumn` = `key`;
  // Uses YBConsistencyLevel::CONSISTENT_PREFIX as this is default for YQL clients.
  Result<tserver::ReadRequestPB> CreateReadRequest(const TabletId& tablet_id, int32_t key);

  // Creates write request for tablet_id which reflects following query (see
  // client::KeyValueTableTest for schema and kXxx constants):
  // INSERT INTO `kTableName`(`kValueColumn`) VALUES (`value`);
  tserver::WriteRequestPB CreateInsertRequest(
      const TabletId& tablet_id, int32_t key, int32_t value);

  // Writes `num_rows` rows into test table using `CreateInsertRequest`.
  // Returns a pair with min and max hash code written.
  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(size_t num_rows);

  Result<scoped_refptr<master::TabletInfo>> GetSingleTestTabletInfo(
      const master::Master& leader_master);

  void WaitForTabletSplitCompletion();

  // Checks all tablet replicas expect ones which have been split to have all rows from 1 to
  // `num_rows` and nothing else.
  void CheckTabletReplicasData(size_t num_rows);

  // Checks source tablet behaviour after split:
  // - It should reject reads and writes.
  void CheckSourceTabletAfterSplit(const TabletId& source_tablet_id);

 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

namespace {

static constexpr auto kRpcTimeout = 15s * kTimeMultiplier;

} // namespace

Result<tserver::ReadRequestPB> TabletSplitITest::CreateReadRequest(
    const TabletId& tablet_id, int32_t key) {
  tserver::ReadRequestPB req;
  auto op = client::CreateReadOp(key, table_, kValueColumn);
  auto* ql_batch = req.add_ql_batch();
  *ql_batch = op->request();

  std::string partition_key;
  RETURN_NOT_OK(op->GetPartitionKey(&partition_key));
  const auto& hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
  ql_batch->set_hash_code(hash_code);
  ql_batch->set_max_hash_code(hash_code);
  req.set_tablet_id(tablet_id);
  req.set_consistency_level(YBConsistencyLevel::CONSISTENT_PREFIX);
  return req;
}

tserver::WriteRequestPB TabletSplitITest::CreateInsertRequest(
    const TabletId& tablet_id, int32_t key, int32_t value) {
  tserver::WriteRequestPB req;
  auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);

  {
    auto op_req = op->mutable_request();
    QLAddInt32HashValue(op_req, key);
    table_.AddInt32ColumnValue(op_req, kValueColumn, value);
  }

  auto* ql_batch = req.add_ql_write_batch();
  *ql_batch = op->request();

  std::string partition_key;
  EXPECT_OK(op->GetPartitionKey(&partition_key));
  const auto& hash_code = PartitionSchema::DecodeMultiColumnHashValue(partition_key);
  ql_batch->set_hash_code(hash_code);
  req.set_tablet_id(tablet_id);
  return req;
}

Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> TabletSplitITest::WriteRows(
    size_t num_rows) {
  auto min_hash_code = std::numeric_limits<docdb::DocKeyHash>::max();
  auto max_hash_code = std::numeric_limits<docdb::DocKeyHash>::min();

  LOG(INFO) << "Writing data...";

  auto session = CreateSession();
  for (auto i = 1; i <= num_rows; ++i) {
    client::YBqlWriteOpPtr op =
        VERIFY_RESULT(WriteRow(session, i /* key */, i /* value */, client::WriteOpType::INSERT));
    const auto hash_code = op->GetHashCode();
    min_hash_code = std::min(min_hash_code, hash_code);
    max_hash_code = std::max(max_hash_code, hash_code);
  }

  LOG(INFO) << "Data has been written";
  LOG(INFO) << "min_hash_code = " << min_hash_code;
  LOG(INFO) << "max_hash_code = " << max_hash_code;
  return std::make_pair(min_hash_code, max_hash_code);
}

namespace {

// Registers two new tablets for splitting in `leader_master`. Gets replicas for source tablet from
// `source_tablet_leader` and uses the same replicas for two new tablets.
// `new_tablets_partition` is used to set a partition mapping for new tablets.
std::vector<TabletId> RegisterNewTabletsForSplit(
    const master::Master& leader_master,
    const tablet::TabletPeer& source_tablet_leader,
    const std::vector<PartitionPB>& new_tablets_partition) {
  auto* catalog_mgr = leader_master.catalog_manager();
  auto* ts_manager = leader_master.ts_manager();
  master::TSDescriptorVector replicas;
  const auto raft_config = source_tablet_leader.RaftConfig();
  LOG(INFO) << "Source tablet peers: " << AsString(raft_config.peers());
  for (const auto& peer : raft_config.peers()) {
    master::TSDescriptorPtr ts_desc;
    EXPECT_TRUE(ts_manager->LookupTSByUUID(peer.permanent_uuid(), &ts_desc));
    replicas.push_back(ts_desc);
  }

  std::vector<TabletId> new_tablet_ids;
  // TODO(tsplit): this (and some other) logic will be moved to master side as a part of
  // https://github.com/yugabyte/yugabyte-db/issues/1461 and test will be reworked.
  for (const auto& new_tablet_partition : new_tablets_partition) {
    auto* new_tablet_info = EXPECT_RESULT(catalog_mgr->TEST_RegisterNewTablet(
        source_tablet_leader.tablet_id(), new_tablet_partition));
    auto lock = new_tablet_info->LockForWrite();
    EXPECT_OK(catalog_mgr->TEST_SelectReplicasForTablet(replicas, new_tablet_info));
    lock->Commit();
    new_tablet_ids.push_back(new_tablet_info->id());
  }
  EXPECT_EQ(new_tablet_ids.size(), 2);
  return new_tablet_ids;
}

// Invoke SplitTablet RPC for `source_tablet_id` on `mini_ts` tablet server using.
// `split_hash_code` and `split_partition_key` are passed inside request to define new key
// boundaries for new tablets.
bool InvokeSplitTablet(
    rpc::ProxyCache* proxy_cache,
    tserver::MiniTabletServer* mini_ts,
    const TabletId& source_tablet_id,
    const std::vector<TabletId>& new_tablet_ids,
    const docdb::DocKeyHash& split_hash_code,
    const std::string& split_partition_key) {
  docdb::KeyBytes split_encoded_key;
  docdb::DocKeyEncoderAfterTableIdStep(&split_encoded_key)
      .Hash(split_hash_code, std::vector<docdb::PrimitiveValue>());

  auto* ts = mini_ts->server();

  tserver::SplitTabletRequestPB req;
  req.set_dest_uuid(ts->permanent_uuid());
  req.set_tablet_id(source_tablet_id);
  req.set_new_tablet1_id(new_tablet_ids[0]);
  req.set_new_tablet2_id(new_tablet_ids[1]);
  req.set_split_partition_key(split_partition_key);
  req.set_split_encoded_key(split_encoded_key.data());

  tserver::SplitTabletResponsePB resp;

  auto ts_admin_proxy = std::make_unique<tserver::TabletServerAdminServiceProxy>(
      proxy_cache, HostPort::FromBoundEndpoint(mini_ts->bound_rpc_addr()));

  LOG(INFO) << "Invoking SplitTablet, source tablet: " << source_tablet_id
            << ", split tablets: " << AsString(new_tablet_ids);
  rpc::RpcController rpc;
  rpc.set_timeout(5s * kTimeMultiplier);
  ts_admin_proxy->SplitTablet(req, &resp, &rpc);

  if (resp.has_error()) {
    LOG(WARNING) << AsString(resp.error());
    return false;
  } else {
    return true;
  }
}

}  // namespace

void TabletSplitITest::WaitForTabletSplitCompletion() {
  ASSERT_OK(WaitFor([this] {
      auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
      size_t num_peers_running = 0;
      size_t num_peers_split = 0;
      size_t num_peers_leader_ready = 0;
      for (const auto& peer : peers) {
        if (!peer->tablet()) {
          break;
        }
        const auto raft_group_state = peer->state();
        const auto tablet_data_state = peer->tablet()->metadata()->tablet_data_state();
        const auto leader_status = peer->consensus()->GetLeaderStatus();
        LOG(INFO) << "T " << peer->tablet_id() << " P " << peer->permanent_uuid()
                << " raft_group_state: " << AsString(raft_group_state)
                << " tablet_data_state: " << AsString(tablet_data_state)
                << " leader status: " << AsString(leader_status);
        if (raft_group_state == tablet::RaftGroupStatePB::RUNNING) {
          ++num_peers_running;
        } else {
          return false;
        }
        num_peers_leader_ready += leader_status == consensus::LeaderStatus::LEADER_AND_READY;
        num_peers_split += tablet_data_state == tablet::TabletDataState::TABLET_DATA_SPLIT;
      }
      LOG(INFO) << "num_peers_running: " << num_peers_running;
      LOG(INFO) << "num_peers_split: " << num_peers_split;
      LOG(INFO) << "num_peers_leader_ready: " << num_peers_leader_ready;
      const auto replication_factor = cluster_->num_tablet_servers();
      // We expect 3 tablets: 1 original tablet + 2 new after-split tablets.
      const auto expected_num_tablets = 3;
      return num_peers_running == replication_factor * expected_num_tablets &&
             num_peers_split == replication_factor &&
             num_peers_leader_ready == expected_num_tablets;
    }, 10s, "Wait for tablet split to be completed"));
}

void TabletSplitITest::CheckTabletReplicasData(size_t num_rows) {
  const auto replication_factor = cluster_->num_tablet_servers();

  std::vector<size_t> keys(num_rows, replication_factor);
  const auto key_column_id = table_.ColumnId(kKeyColumn);
  const auto value_column_id = table_.ColumnId(kValueColumn);
  for (auto peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    const auto* tablet = peer->tablet();
    if (tablet->metadata()->tablet_data_state() != tablet::TabletDataState::TABLET_DATA_SPLIT) {
      const Schema& schema = tablet->metadata()->schema();
      auto client_schema = schema.CopyWithoutColumnIds();
      auto iter = ASSERT_RESULT(tablet->NewRowIterator(client_schema, boost::none));
      QLTableRow row;
      std::unordered_set<size_t> tablet_keys;
      while (ASSERT_RESULT(iter->HasNext())) {
        ASSERT_OK(iter->NextRow(&row));
        auto key_opt = row.GetValue(key_column_id);
        ASSERT_TRUE(key_opt.is_initialized());
        ASSERT_EQ(key_opt, row.GetValue(value_column_id));
        auto key = key_opt->int32_value();
        ASSERT_TRUE(tablet_keys.insert(key).second)
            << "Duplicate key " << key << " in tablet " << tablet->tablet_id();
        ASSERT_GT(keys[key - 1]--, 0)
            << "Extra key " << key << " in tablet " << tablet->tablet_id();
      }
    }
  }
  for (auto key = 1; key <= num_rows; ++key) {
    ASSERT_EQ(keys[key - 1], 0) << "Missing key: " << key;
  }
}

void TabletSplitITest::CheckSourceTabletAfterSplit(const TabletId& source_tablet_id) {
  FLAGS_do_not_start_election_test_only = true;
  size_t tablet_split_insert_error_count = 0;
  size_t not_the_leader_insert_error_count = 0;
  for (auto mini_ts : cluster_->mini_tablet_servers()) {
    auto ts_service_proxy = std::make_unique<tserver::TabletServerServiceProxy>(
        proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_ts->bound_rpc_addr()));

    {
      tserver::ReadRequestPB req = ASSERT_RESULT(CreateReadRequest(source_tablet_id, 1 /* key */));

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::ReadResponsePB resp;
      ts_service_proxy->Read(req, &resp, &controller);

      ASSERT_TRUE(resp.has_error());
      ASSERT_EQ(resp.error().code(), tserver::TabletServerErrorPB::TABLET_SPLIT);
    }

    {
      tserver::WriteRequestPB req =
          CreateInsertRequest(source_tablet_id, 0 /* key */, 0 /* value */);

      rpc::RpcController controller;
      controller.set_timeout(kRpcTimeout);
      tserver::WriteResponsePB resp;
      ts_service_proxy->Write(req, &resp, &controller);

      ASSERT_TRUE(resp.has_error());
      LOG(INFO) << "Error: " << AsString(resp.error());
      switch (resp.error().code()) {
        case tserver::TabletServerErrorPB::UNKNOWN_ERROR:
          ASSERT_EQ(resp.error().status().code(), AppStatusPB::TRY_AGAIN_CODE);
          tablet_split_insert_error_count++;
          break;
        case tserver::TabletServerErrorPB::NOT_THE_LEADER:
          not_the_leader_insert_error_count++;
          break;
        default:
          FAIL() << "Unexpected error: " << AsString(resp.error());
      }
    }
  }
  // Leader should return "try again" error on insert.
  ASSERT_EQ(tablet_split_insert_error_count, 1);
  // Followers should return "not the leader" error.
  ASSERT_EQ(not_the_leader_insert_error_count, cluster_->num_tablet_servers() - 1);
}

Result<scoped_refptr<master::TabletInfo>> TabletSplitITest::GetSingleTestTabletInfo(
    const master::Master& leader_master) {
  std::vector<scoped_refptr<master::TabletInfo>> tablet_infos;
  leader_master.catalog_manager()->GetTableInfo(table_->id())->GetAllTablets(&tablet_infos);

  SCHECK_EQ(tablet_infos.size(), 1, IllegalState, "Expect test table to have only 1 tablet");
  return tablet_infos.front();
}

namespace {

std::vector<PartitionPB> CreateNewTabletsPartition(
    master::TabletInfo* tablet_info, const std::string split_partition_key) {
  const auto& source_partition = tablet_info->LockForRead()->data().pb.partition();

  std::vector<PartitionPB> new_tablets_partition;

  new_tablets_partition.push_back(source_partition);
  new_tablets_partition.back().set_partition_key_end(split_partition_key);

  new_tablets_partition.push_back(source_partition);
  new_tablets_partition.back().set_partition_key_start(split_partition_key);

  return new_tablets_partition;
}

} // namespace

// Tests splitting of the single tablet in following steps:
// 1. Creates single-tablet table and populates it with specified number of rows.
// 2. Send SplitTablet RPC to the tablet leader.
// 3. After tablet split is completed - check that new tablets have exactly the same rows.
// 4. Check that source tablet is rejecting reads and writes.
TEST_F(TabletSplitITest, SplitSingleTablet) {
  constexpr auto kNumRows = 1000;

  CreateTable(client::Transactional::kFalse, 1 /* num_tablets */, client_.get(), &table_);
  auto min_max_hash_code = ASSERT_RESULT(WriteRows(kNumRows));

  const auto split_hash_code = (min_max_hash_code.first + min_max_hash_code.second) / 2;
  LOG(INFO) << "Split hash code: " << split_hash_code;

  const auto split_partition_key = PartitionSchema::EncodeMultiColumnHashValue(split_hash_code);

  auto& leader_master = *ASSERT_NOTNULL(cluster_->leader_mini_master()->master());

  auto source_tablet_info = ASSERT_RESULT(GetSingleTestTabletInfo(leader_master));
  const auto source_tablet_id = source_tablet_info->id();

  auto new_tablets_partition =
      CreateNewTabletsPartition(source_tablet_info.get(), split_partition_key);

  for (;;) {
    auto* mini_ts = FindTabletLeader(cluster_.get(), source_tablet_id);
    if (!mini_ts) {
      std::this_thread::sleep_for(500ms);
      continue;
    }
    LOG(INFO) << Format(
        "Found leader ts $0 for source tablet $1", mini_ts->server()->permanent_uuid(),
        source_tablet_id);

    auto* ts = mini_ts->server();
    std::shared_ptr<tablet::TabletPeer> tablet_peer;
    ASSERT_OK(ts->tablet_manager()->GetTabletPeer(source_tablet_id, &tablet_peer));

    std::vector<TabletId> new_tablet_ids =
        RegisterNewTabletsForSplit(leader_master, *tablet_peer, new_tablets_partition);
    NO_PENDING_FATALS();

    if (InvokeSplitTablet(
            proxy_cache_.get(), mini_ts, source_tablet_id, new_tablet_ids, split_hash_code,
            split_partition_key)) {
      break;
    }
  }

  WaitForTabletSplitCompletion();

  CheckTabletReplicasData(kNumRows);

  CheckSourceTabletAfterSplit(source_tablet_id);
}

}  // namespace yb
