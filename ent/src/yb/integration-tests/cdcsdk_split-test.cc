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

#include "yb/integration-tests/cdcsdk_test_base.h"

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_util.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/doc_key.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/result.h"
#include "yb/util/test_util.h"

DECLARE_bool(force_tablet_split_of_cdcsdk_tables);

namespace yb {

using client::YBClient;
using client::YBTableName;

namespace cdc {
namespace enterprise {

class NotSupportedTabletSplitITest : public CDCSDKTestBase {
 public:
  Status SetUpWithParams() {
    RETURN_NOT_OK(CDCSDKTestBase::SetUpWithParams(this->replication_factor));
    this->test_table = VERIFY_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName, 1));

    LOG(INFO) << "Table with a single tablet created successfully for "
              << "CDCSDK Split Not Supported Test";

    // Create a cdc stream for this tablet.
    std::string stream_id = VERIFY_RESULT(CreateDBStream());

    LOG(INFO) << "Created a CDC stream for namespace " << kNamespaceName << " with stream id "
              << stream_id;

    return Status::OK();
  }

 protected:
  static constexpr int kDefaultNumRows = 500;
  MonoDelta split_completion_timeout_ = std::chrono::seconds(40) * kTimeMultiplier;
  static constexpr int replication_factor = 3;

  YBTableName test_table;

  Result<master::CatalogManagerIf*> catalog_manager() {
    return &CHECK_NOTNULL(VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMiniMaster()))
                ->catalog_manager();
  }

  // Supporting functions to split and validate a tablet.
  Result<std::string> GetSingleTestTabletInfo(master::CatalogManagerIf* catalog_mgr) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;

    RETURN_NOT_OK(test_client()->GetTablets(this->test_table, 0, &tablets,
        /* partition_list_version = */ nullptr));

    SCHECK_EQ(tablets.size(), 1U, IllegalState, "Expect test table to have only 1 tablet");
    return tablets.Get(0).tablet_id();
  }

  Status DoSplitTablet(master::CatalogManagerIf* catalog_mgr, const tablet::Tablet& tablet) {
    const auto& tablet_id = tablet.tablet_id();
    LOG(INFO) << "Tablet: " << tablet_id;
    LOG(INFO) << "Number of SST files: " << tablet.TEST_db()->GetCurrentVersionNumSSTFiles();
    std::string properties;
    tablet.TEST_db()->GetProperty(rocksdb::DB::Properties::kAggregatedTableProperties, &properties);
    LOG(INFO) << "DB properties: " << properties;

    const auto encoded_split_key = VERIFY_RESULT(tablet.GetEncodedMiddleSplitKey());
    std::string partition_split_key = encoded_split_key;
    if (tablet.metadata()->partition_schema()->IsHashPartitioning()) {
      const auto doc_key_hash = VERIFY_RESULT(docdb::DecodeDocKeyHash(encoded_split_key)).value();
      LOG(INFO) << "Middle hash key: " << doc_key_hash;
      partition_split_key = PartitionSchema::EncodeMultiColumnHashValue(doc_key_hash);
    }
    LOG(INFO) << "Partition split key: " << Slice(partition_split_key).ToDebugHexString();

    return catalog_mgr->TEST_SplitTablet(tablet_id, encoded_split_key, partition_split_key);
  }

  Result<TabletId> SplitSingleTablet() {
    auto* catalog_mgr = VERIFY_RESULT(catalog_manager());

    auto source_tablet_id = VERIFY_RESULT(GetSingleTestTabletInfo(catalog_mgr));

    VLOG(1) << "Tablet for test: " << source_tablet_id;

    std::vector<tablet::TabletPeerPtr> peers =
        ListTabletPeers(test_cluster_.mini_cluster_.get(), ListPeersFilter::kAll);

    for (const auto& peer : peers) {
      const auto tablet = peer->shared_tablet();
      if (tablet->tablet_id() == source_tablet_id) {
        VLOG(1) << "Processing " << tablet->tablet_id();
        RETURN_NOT_OK(DoSplitTablet(catalog_mgr, *tablet));
      }
    }

    return source_tablet_id;
  }

  void WriteRowsInTransactionAndFlush(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    EXPECT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      EXPECT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
    EXPECT_OK(conn.Execute("COMMIT"));
    EXPECT_OK(this->test_cluster_.client_->FlushTables({this->test_table}, false, 30, true));
  }

  Status WaitForTabletSplitCompletion(
      const size_t expected_non_split_tablets,
      const size_t expected_split_tablets,
      size_t num_replicas_online,
      const client::YBTableName& table
    ) {
    if (num_replicas_online == 0) {
      num_replicas_online = FLAGS_replication_factor;
    }

    VLOG(1) << "Waiting for tablet split to be completed... ";
    VLOG(1) << "expected_non_split_tablets: " << expected_non_split_tablets;
    VLOG(1) << "expected_split_tablets: " << expected_split_tablets;

    const auto expected_total_tablets = expected_non_split_tablets + expected_split_tablets;
    VLOG(1) << "expected_total_tablets: " << expected_total_tablets;

    std::vector<tablet::TabletPeerPtr> peers;
    auto s = WaitFor(
        [&] {
          peers = ListTabletPeers(test_cluster_.mini_cluster_.get(), ListPeersFilter::kAll);
          size_t num_peers_running = 0;
          size_t num_peers_split = 0;
          size_t num_peers_leader_ready = 0;
          for (const auto& peer : peers) {
            const auto tablet = peer->shared_tablet();
            const auto consensus = peer->shared_consensus();
            if (!tablet || !consensus) {
              break;
            }
            if (tablet->metadata()->table_name() != table.table_name() ||
                tablet->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
              continue;
            }
            const auto raft_group_state = peer->state();
            const auto tablet_data_state = tablet->metadata()->tablet_data_state();
            const auto leader_status = consensus->GetLeaderStatus(/* allow_stale =*/true);
            if (raft_group_state == tablet::RaftGroupStatePB::RUNNING) {
              ++num_peers_running;
            } else {
              return false;
            }
            num_peers_leader_ready += leader_status == consensus::LeaderStatus::LEADER_AND_READY;
            num_peers_split += tablet_data_state
                               == tablet::TabletDataState::TABLET_DATA_SPLIT_COMPLETED;
          }
          VLOG(1) << "num_peers_running: " << num_peers_running;
          VLOG(1) << "num_peers_split: " << num_peers_split;
          VLOG(1) << "num_peers_leader_ready: " << num_peers_leader_ready;

          return num_peers_running == num_replicas_online * expected_total_tablets &&
                 num_peers_split == num_replicas_online * expected_split_tablets &&
                 num_peers_leader_ready == expected_total_tablets;
        },
        split_completion_timeout_, "Wait for tablet split to be completed");
    if (!s.ok()) {
      for (const auto& peer : peers) {
        const auto tablet = peer->shared_tablet();
        const auto consensus = peer->shared_consensus();
        if (!tablet || !consensus) {
          LOG(INFO) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                    << "no tablet";
          continue;
        }
        if (tablet->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
          continue;
        }
        LOG(INFO) << consensus::MakeTabletLogPrefix(peer->tablet_id(), peer->permanent_uuid())
                  << "raft_group_state: " << AsString(peer->state()) << " tablet_data_state: "
                  << TabletDataState_Name(tablet->metadata()->tablet_data_state())
                  << " leader status: "
                  << AsString(consensus->GetLeaderStatus(/* allow_stale =*/true));
      }
    }
    LOG(INFO) << "Waiting for tablet split to be completed - DONE";
    return Status::OK();
  }
};

TEST_F(NotSupportedTabletSplitITest, SplittingUnsupportedWithCdcSdkStream) {
  ASSERT_OK(SetUpWithParams());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_tablet_split_of_cdcsdk_tables) = false;

  // Try splitting this tablet.
  WriteRowsInTransactionAndFlush(1, kDefaultNumRows, &test_cluster_);
  auto s = SplitSingleTablet();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.status().IsNotSupported()) << s.status();
}

TEST_F(NotSupportedTabletSplitITest, SplittingAllowedWithFlagAndCdcSdkStream) {
  ASSERT_OK(SetUpWithParams());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_tablet_split_of_cdcsdk_tables) = true;

  WriteRowsInTransactionAndFlush(1, kDefaultNumRows, &test_cluster_);
  ASSERT_OK(SplitSingleTablet());
  EXPECT_OK(WaitForTabletSplitCompletion(2, 2, replication_factor, test_table));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  EXPECT_OK(test_client()->GetTablets(this->test_table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 2);
}

}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
