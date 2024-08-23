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

#pragma once

#include <chrono>

#include "yb/client/client_fwd.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/txn-test-base.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/integration-tests/create-table-itest-base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/size_literals.h"
#include "yb/util/tsan_util.h"

namespace yb {

class TestWorkload;

namespace client {

class SnapshotTestUtil;

}

void DumpTableLocations(
    master::CatalogManagerIf* catalog_mgr, const client::YBTableName& table_name);

void DumpWorkloadStats(const TestWorkload& workload);

Status SplitTablet(master::CatalogManagerIf* catalog_mgr, const tablet::Tablet& tablet);

Status DoSplitTablet(master::CatalogManagerIf* catalog_mgr, const tablet::Tablet& tablet);

template <class MiniClusterType>
class TabletSplitITestBase : public client::TransactionTestBase<MiniClusterType> {
 protected:
  static constexpr std::chrono::duration<int64> kRpcTimeout =
      std::chrono::seconds(60) * kTimeMultiplier;
  static constexpr int kDefaultNumRows = 500;
  // We set small data block size, so we don't have to write much data to have multiple blocks.
  // We need multiple blocks to be able to detect split key (see BlockBasedTable::GetMiddleKey).
  static constexpr size_t kDbBlockSizeBytes = 2_KB;
 public:
  void SetUp() override;

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

  // Writes `num_rows` rows into the specified table using `CreateInsertRequest`.
  // Returns a pair with min and max hash code written.
  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(
      client::TableHandle* table, uint32_t num_rows, int32_t start_key, int32_t start_value,
      client::YBSessionPtr session = nullptr);

  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(
      client::TableHandle* table, uint32_t num_rows = 2000, int32_t start_key = 1,
      client::YBSessionPtr session = nullptr) {
    return WriteRows(table, num_rows, start_key, start_key, session);
  }

  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRows(
      uint32_t num_rows = 2000, int32_t start_key = 1) {
    return WriteRows(&this->table_, num_rows, start_key);
  }

  // Waits for intents of test table to be applied.
  Status WaitForTestTableIntentsApplied();

  Status FlushTable(const TableId& table_id);
  Status FlushTestTable();

  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRowsAndFlush(
      client::TableHandle* table, uint32_t num_rows = kDefaultNumRows, int32_t start_key = 1,
      bool wait_for_intents = true);

  Result<std::pair<docdb::DocKeyHash, docdb::DocKeyHash>> WriteRowsAndFlush(
      uint32_t num_rows = kDefaultNumRows, int32_t start_key = 1, bool wait_for_intents = true);

  Result<docdb::DocKeyHash> WriteRowsAndGetMiddleHashCode(
      uint32_t num_rows, bool wait_for_intents = true);

  Result<std::shared_ptr<master::TabletInfo>> GetSingleTestTabletInfo(
      master::CatalogManagerIf* catalog_manager);

  void CreateSingleTablet() {
    this->SetNumTablets(1);
    this->CreateTable();
  }

  Status CheckRowsCount(size_t expected_num_rows) {
    auto rows_count = VERIFY_RESULT(CountRows(this->NewSession(), this->table_));
    SCHECK_EQ(rows_count, expected_num_rows, InternalError, "Got unexpected rows count");
    return Status::OK();
  }

  Result<TableId> GetTestTableId() {
    return GetTableId(this->client_.get(), client::kTableName);
  }

  // Make sure table contains only keys 1...num_keys without gaps.
  void CheckTableKeysInRange(const size_t num_keys);

  Result<bool> IsSplittingComplete(
      yb::master::MasterAdminProxy* master_proxy, bool wait_for_parent_deletion);

 protected:
  virtual int64_t GetRF() { return 3; }

  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  MonoDelta split_completion_timeout_sec_ = std::chrono::seconds(40) * kTimeMultiplier;
};
// Let compiler know about these explicit specializations since below subclasses inherit from them.
extern template class TabletSplitITestBase<MiniCluster>;
extern template class TabletSplitITestBase<ExternalMiniCluster>;


class TabletSplitITest : public TabletSplitITestBase<MiniCluster> {
 public:
  TabletSplitITest();
  ~TabletSplitITest();

  void SetUp() override;

  Result<TabletId> CreateSingleTabletAndSplit(uint32_t num_rows, bool wait_for_intents = true);

  Result<master::Master&> GetLeaderMaster() {
    return *CHECK_NOTNULL(VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master());
  }

  Result<master::CatalogManagerIf*> catalog_manager() {
    return VERIFY_RESULT(GetLeaderMaster()).get().catalog_manager();
  }

  Result<master::TabletInfos> GetTabletInfosForTable(const TableId& table_id);

  // By default we wait until all split tablets are cleanup. expected_split_tablets could be
  // overridden if needed to test behaviour of split tablet when its deletion is disabled.
  // If num_replicas_online is 0, uses replication factor.
  Status WaitForTabletSplitCompletion(
      const size_t expected_non_split_tablets, const size_t expected_split_tablets = 0,
      size_t num_replicas_online = 0, const client::YBTableName& table = client::kTableName,
      bool core_dump_on_failure = true);

  Result<tserver::GetSplitKeyResponsePB> SendTServerRpcSyncGetSplitKey(const TabletId& tablet_id);

  Result<master::SplitTabletResponsePB> SendMasterRpcSyncSplitTablet(const TabletId& tablet_id);

  Result<TabletId> SplitSingleTablet(docdb::DocKeyHash split_hash_code);

  Result<TabletId> SplitTabletAndValidate(
      docdb::DocKeyHash split_hash_code,
      size_t num_rows,
      bool parent_tablet_protected_from_deletion = false);

  // Checks source tablet behaviour after split:
  // - It should reject reads and writes.
  Status CheckSourceTabletAfterSplit(const TabletId& source_tablet_id);

  // Tests appropriate client requests structure update at YBClient side.
  // split_depth specifies how deep should we split original tablet until trying to write again.
  void SplitClientRequestsIds(int split_depth);

  // Returns all tablet peers in the cluster which are marked as being in
  // TABLET_DATA_SPLIT_COMPLETED state. In most of the test cases below, this corresponds to the
  // post-split parent/source tablet peers.
  Result<std::vector<tablet::TabletPeerPtr>> ListSplitCompleteTabletPeers();

  // Returns all tablet peers in the cluster which are not part of a transaction table
  // and which are Active (refer to IsActive). In most of the test cases below,
  // this corresponds to post-split children tablet peers.
  Result<std::vector<tablet::TabletPeerPtr>> ListTestTableActiveTabletPeers();

  // Wait for all peers to complete post-split compaction.
  Status WaitForTestTableTabletPeersPostSplitCompacted(MonoDelta timeout);

  Result<int> NumTestTableTabletPeersPostSplitCompacted();

  // Returns the smallest sst file size among all replicas for a given tablet id
  Result<uint64_t> GetMinSstFileSizeAmongAllReplicas(const std::string& tablet_id);

  // Checks active tablet replicas (all expect ones that have been split) to have all rows from 1 to
  // `num_rows` and nothing else.
  // If num_replicas_online is 0, uses replication factor.
  Status CheckPostSplitTabletReplicasData(
      size_t num_rows, size_t num_replicas_online = 0, size_t num_active_tablets = 2);

  Status WaitForTableNumActiveLeadersPeers(size_t expected_leaders);

 protected:
  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
};


class TabletSplitExternalMiniClusterITest : public TabletSplitITestBase<ExternalMiniCluster> {
 public:
  void SetFlags() override;

  Status SplitTablet(const std::string& tablet_id);

  Status FlushTabletsOnSingleTServer(
      size_t tserver_idx, const std::vector<yb::TabletId> tablet_ids, bool is_compaction);

  Result<std::set<TabletId>> GetTestTableTabletIds(size_t tserver_idx);

  Result<std::set<TabletId>> GetTestTableTabletIds();

  Result<std::vector<tserver::ListTabletsResponsePB_StatusAndSchemaPB>> ListTablets(
      size_t tserver_idx);

  Result<std::vector<tserver::ListTabletsResponsePB_StatusAndSchemaPB>> ListTablets();

  Status WaitForTabletsExcept(
      size_t num_tablets, size_t tserver_idx, const TabletId& exclude_tablet);

  Status WaitForTablets(size_t num_tablets, size_t tserver_idx);

  Status WaitForTablets(size_t num_tablets);

  Status WaitForAnySstFiles(const TabletId& tablet_id);
  Status WaitForAnySstFiles(size_t tserver_idx, const TabletId& tablet_id);
  Status WaitForAnySstFiles(const ExternalTabletServer& ts, const TabletId& tablet_id);

  Status WaitTServerToBeQuietOnTablet(
      itest::TServerDetails* ts_desc, const TabletId& tablet_id);

  Status SplitTabletCrashMaster(bool change_split_boundary, std::string* split_partition_key);

  Result<TabletId> GetOnlyTestTabletId(size_t tserver_idx);

  Result<TabletId> GetOnlyTestTabletId();
};

}  // namespace yb
