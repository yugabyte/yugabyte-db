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

#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/tablet_peer.h"

DECLARE_uint32(cdc_wal_retention_time_secs);
DECLARE_uint32(max_xcluster_streams_to_checkpoint_in_parallel);
DECLARE_bool(TEST_block_xcluster_checkpoint_namespace_task);
namespace yb {
namespace master {

const auto kDeadline = MonoDelta::FromSeconds(30);
const NamespaceName kNamespaceName = "db1";
const PgSchemaName kPgSchemaName = "public";
const xcluster::ReplicationGroupId kReplicationGroupId("rg1");
const TableName kTableName1 = "table1", kTableName2 = "table2";

class XClusterOutboundReplicationGroupTest : public XClusterYsqlTestBase {
 public:
  XClusterOutboundReplicationGroupTest() {}
  void SetUp() override {
    XClusterYsqlTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.num_masters = 1;
    ASSERT_OK(InitProducerClusterOnly(opts));
    client_ = producer_client();

    catalog_manager_ =
        &ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->catalog_manager_impl();
    epoch_ = catalog_manager_->GetLeaderEpochInternal();

    namespace_id_ = ASSERT_RESULT(CreateYsqlNamespace(kNamespaceName));
  }

  Result<NamespaceId> CreateYsqlNamespace(const NamespaceName& ns_name) {
    CreateNamespaceResponsePB resp;
    RETURN_NOT_OK(CreateDatabase(&producer_cluster_, ns_name));
    return GetNamespaceId(client_, ns_name);
  }

  Result<TableId> CreateYsqlTable(
      const NamespaceName& namespace_name, const TableName& table_name,
      const PgSchemaName& schema_name = kPgSchemaName) {
    RETURN_NOT_OK(XClusterYsqlTestBase::CreateYsqlTable(
        &producer_cluster_, namespace_name, schema_name, table_name,
        boost::none /* tablegroup_name */, 1 /* num_tablets */));
    auto table_info = catalog_manager_->GetTableInfoFromNamespaceNameAndTableName(
        YQLDatabase::YQL_DATABASE_PGSQL, namespace_name, table_name, schema_name);
    SCHECK(table_info, NotFound, "Table create failed", table_name);

    return table_info->id();
  }

  // Cleanup streams marked for deletion and get the list of xcluster streams.
  std::unordered_set<xrepl::StreamId> CleanupAndGetAllXClusterStreams() {
    catalog_manager_->RunXReplBgTasks(epoch_);
    return catalog_manager_->GetAllXReplStreamIds();
  }

  void VerifyNamespaceCheckpointInfo(
      const TableId& table_id1, const TableId& table_id2, size_t all_xcluster_streams_count,
      const master::GetXClusterStreamsResponsePB& resp, bool skip_schema_name_check = false) {
    ASSERT_FALSE(resp.initial_bootstrap_required());
    ASSERT_EQ(resp.table_infos_size(), 2);

    auto all_xcluster_streams = CleanupAndGetAllXClusterStreams();
    ASSERT_EQ(all_xcluster_streams.size(), all_xcluster_streams_count);

    std::set<TableId> table_ids;
    for (const auto& table_info : resp.table_infos()) {
      if (table_info.table_name() == kTableName1) {
        ASSERT_EQ(table_info.table_id(), table_id1);
      } else if (table_info.table_name() == kTableName2) {
        ASSERT_EQ(table_info.table_id(), table_id2);
      } else {
        FAIL() << "Unexpected table name: " << table_info.table_name();
      }
      if (skip_schema_name_check) {
        // Make sure it is not empty.
        ASSERT_FALSE(table_info.pg_schema_name().empty());
      } else {
        ASSERT_EQ(table_info.pg_schema_name(), kPgSchemaName);
      }
      ASSERT_FALSE(table_info.xrepl_stream_id().empty());
      auto stream_id = ASSERT_RESULT(xrepl::StreamId::FromString(table_info.xrepl_stream_id()));
      ASSERT_TRUE(all_xcluster_streams.contains(stream_id));

      table_ids.insert(table_info.table_id());
    }
    ASSERT_TRUE(table_ids.contains(table_id1));
    ASSERT_TRUE(table_ids.contains(table_id2));
  }

  Result<master::GetXClusterStreamsResponsePB> GetXClusterStreams(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      std::vector<TableName> table_names = {}, std::vector<PgSchemaName> pg_schema_names = {}) {
    std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
    RETURN_NOT_OK(XClusterClient().GetXClusterStreams(
        CoarseMonoClock::Now() + kDeadline, replication_group_id, namespace_id, table_names,
        pg_schema_names, [&promise](const auto& resp) { promise.set_value(resp); }));

    return promise.get_future().get();
  }

  Result<master::GetXClusterStreamsResponsePB> GetXClusterStreamsByTableId(
      const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
      std::vector<TableId> table_ids) {
    std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
    RETURN_NOT_OK(XClusterClient().GetXClusterStreams(
        CoarseMonoClock::Now() + kDeadline, replication_group_id, namespace_id, table_ids,
        [&promise](const auto& resp) { promise.set_value(resp); }));

    return promise.get_future().get();
  }

  Status VerifyWalRetentionOfTable(
      const TableId& table_id, uint32 wal_retention_secs = FLAGS_cdc_wal_retention_time_secs) {
    auto tablets = ListTableActiveTabletLeadersPeers(producer_cluster(), table_id);
    SCHECK_GE(
        tablets.size(), static_cast<size_t>(1), IllegalState,
        Format("No active tablets found for table $0", table_id));
    for (const auto& tablet : tablets) {
      SCHECK_EQ(
          tablet->tablet_metadata()->wal_retention_secs(), wal_retention_secs, IllegalState,
          Format("Tablet: $0", tablet->tablet_metadata()->LogPrefix()));
    }

    return Status::OK();
  }

  Status RestartMaster() {
    auto master = VERIFY_RESULT(producer_cluster()->GetLeaderMiniMaster());
    RETURN_NOT_OK(master->Restart());
    catalog_manager_ = &master->catalog_manager_impl();
    epoch_ = catalog_manager_->GetLeaderEpochInternal();
    return Status::OK();
  }

  client::XClusterClient XClusterClient() { return client::XClusterClient(*client_); }

  CatalogManager* catalog_manager_;
  LeaderEpoch epoch_;
  YBClient* client_;
  NamespaceId namespace_id_;
};

TEST_F(XClusterOutboundReplicationGroupTest, TestMultipleTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) = 1;

  // Create two tables in two schemas.
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  PgSchemaName pg_schema_name2 = "myschema";
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2, pg_schema_name2));

  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  auto resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  // We should have 2 streams now.
  size_t stream_count = 2;
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      table_id_1, table_id_2, stream_count, resp, /*skip_schema_name_check=*/true));

  for (const auto& table_info : resp.table_infos()) {
    // Order is not deterministic so search with the table name.
    if (table_info.table_name() == kTableName1) {
      ASSERT_EQ(table_info.pg_schema_name(), kPgSchemaName);
    } else {
      ASSERT_EQ(table_info.pg_schema_name(), pg_schema_name2);
    }
  }

  // Get the table info in a custom order.
  resp = ASSERT_RESULT(GetXClusterStreams(
      kReplicationGroupId, namespace_id_, {kTableName2, kTableName1},
      {pg_schema_name2, kPgSchemaName}));
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      table_id_1, table_id_2, stream_count, resp, /*skip_schema_name_check=*/true));
  ASSERT_EQ(resp.table_infos(0).pg_schema_name(), pg_schema_name2);
  ASSERT_EQ(resp.table_infos(1).pg_schema_name(), kPgSchemaName);
  ASSERT_EQ(resp.table_infos(0).table_name(), kTableName2);
  ASSERT_EQ(resp.table_infos(1).table_name(), kTableName1);

  ASSERT_OK(VerifyWalRetentionOfTable(table_id_1));
  ASSERT_OK(VerifyWalRetentionOfTable(table_id_2));

  ASSERT_OK(XClusterClient().DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/{}));
  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  // We should have 0 streams now.
  auto all_xcluster_streams = CleanupAndGetAllXClusterStreams();
  ASSERT_TRUE(all_xcluster_streams.empty());
}

TEST_F(XClusterOutboundReplicationGroupTest, AddDeleteNamespaces) {
  auto ns1_table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto ns1_table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));

  const NamespaceId namespace_name_2 = "db2";
  const auto namespace_id_2 = ASSERT_RESULT(CreateYsqlNamespace(namespace_name_2));
  auto ns2_table_id_1 = ASSERT_RESULT(CreateYsqlTable(namespace_name_2, kTableName1));
  auto ns2_table_id_2 = ASSERT_RESULT(CreateYsqlTable(namespace_name_2, kTableName2));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  // Wait for the new streams to be ready.
  auto ns1_info = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  // We should have 2 streams now.
  size_t stream_count = 2;
  auto all_xcluster_streams_initial = CleanupAndGetAllXClusterStreams();
  ASSERT_EQ(all_xcluster_streams_initial.size(), 2);

  // Make sure invalid namespace id is handled correctly.
  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, "BadId"));

  // Make sure only the namespace that was added is returned.
  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, namespace_id_2));

  ASSERT_NO_FATALS(
      VerifyNamespaceCheckpointInfo(ns1_table_id_1, ns1_table_id_2, stream_count, ns1_info));

  // Add the second namespace.
  ASSERT_OK(client::XClusterClient(*client_).AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, namespace_id_2));

  // We should have 4 streams now.
  stream_count = 4;

  // The info of the first namespace should not change.
  auto ns1_info_dup = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(ns1_info_dup.ShortDebugString(), ns1_info.ShortDebugString());

  // Validate the seconds namespace.
  auto ns2_info = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_2));
  ASSERT_NO_FATALS(
      VerifyNamespaceCheckpointInfo(ns2_table_id_1, ns2_table_id_2, stream_count, ns2_info));

  ASSERT_OK(XClusterClient().RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, namespace_id_, /*target_master_addresses=*/{}));
  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  // We should only have only the streams from second namespace.
  {
    auto new_xcluster_streams = CleanupAndGetAllXClusterStreams();
    ASSERT_EQ(new_xcluster_streams.size(), 2);

    // new_xcluster_streams and all_xcluster_streams should not overlap.
    for (const auto& stream : new_xcluster_streams) {
      ASSERT_FALSE(all_xcluster_streams_initial.contains(stream));
    }
  }

  ASSERT_OK(XClusterClient().DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/{}));
  ASSERT_NOK(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  auto final_xcluster_streams = CleanupAndGetAllXClusterStreams();
  ASSERT_TRUE(final_xcluster_streams.empty());
}

TEST_F(XClusterOutboundReplicationGroupTest, AddTable) {
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  ASSERT_OK(VerifyWalRetentionOfTable(table_id_1, 0));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  // Wait for the new streams to be ready.
  ASSERT_OK(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  auto all_xcluster_streams_initial = CleanupAndGetAllXClusterStreams();
  ASSERT_EQ(all_xcluster_streams_initial.size(), 1);

  ASSERT_OK(VerifyWalRetentionOfTable(table_id_1));

  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));

  auto ns1_info = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));

  size_t stream_count = 2;
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(table_id_1, table_id_2, stream_count, ns1_info));

  ASSERT_OK(VerifyWalRetentionOfTable(table_id_2));
}

TEST_F(XClusterOutboundReplicationGroupTest, IsBootstrapRequiredEmptyTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) = 1;

  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  std::promise<Result<bool>> promise;

  ASSERT_OK(XClusterClient().IsBootstrapRequired(
      CoarseMonoClock::Now() + kDeadline, kReplicationGroupId, namespace_id_,
      [&promise](Result<bool> result) { promise.set_value(std::move(result)); }));

  auto is_bootstrap_required = ASSERT_RESULT(promise.get_future().get());
  ASSERT_FALSE(is_bootstrap_required);
}

TEST_F(XClusterOutboundReplicationGroupTest, IsBootstrapRequiredTableWithData) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) = 1;

  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));
  std::shared_ptr<client::YBTable> table_2;
  ASSERT_OK(producer_client()->OpenTable(table_id_2, &table_2));
  ASSERT_OK(InsertRowsInProducer(0, 10, table_2));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  std::promise<Result<bool>> promise;

  ASSERT_OK(XClusterClient().IsBootstrapRequired(
      CoarseMonoClock::Now() + kDeadline, kReplicationGroupId, namespace_id_,
      [&promise](Result<bool> result) { promise.set_value(std::move(result)); }));

  auto is_bootstrap_required = ASSERT_RESULT(promise.get_future().get());
  ASSERT_TRUE(is_bootstrap_required);
}

TEST_F(XClusterOutboundReplicationGroupTest, IsBootstrapRequiredTableWithDeletedData) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) = 1;

  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));
  std::shared_ptr<client::YBTable> table_2;
  ASSERT_OK(producer_client()->OpenTable(table_id_2, &table_2));
  ASSERT_OK(InsertRowsInProducer(0, 10, table_2));
  ASSERT_OK(DeleteRowsInProducer(0, 10, table_2));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  std::promise<Result<bool>> promise;

  ASSERT_OK(XClusterClient().IsBootstrapRequired(
      CoarseMonoClock::Now() + kDeadline, kReplicationGroupId, namespace_id_,
      [&promise](Result<bool> result) { promise.set_value(std::move(result)); }));

  auto is_bootstrap_required = ASSERT_RESULT(promise.get_future().get());
  ASSERT_FALSE(is_bootstrap_required);
}

TEST_F(XClusterOutboundReplicationGroupTest, MasterRestartDuringCheckpoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_xcluster_streams_to_checkpoint_in_parallel) = 1;
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = true;

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
  auto future = promise.get_future();
  ASSERT_OK(XClusterClient().GetXClusterStreams(
      CoarseMonoClock::Now() + kDeadline, kReplicationGroupId, namespace_id_, /*table_names=*/{},
      /*pg_schema_names=*/{}, [&promise](const auto& resp) { promise.set_value(resp); }));

  ASSERT_EQ(future.wait_for(5s), std::future_status::timeout);

  ASSERT_OK(RestartMaster());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = false;

  auto resp = ASSERT_RESULT(future.get());
  size_t stream_count = 2;
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(table_id_1, table_id_2, stream_count, resp));

  auto all_xcluster_streams_initial = CleanupAndGetAllXClusterStreams();
  ASSERT_EQ(all_xcluster_streams_initial.size(), stream_count);

  // TODO (#21986) : Disabled since VerifyWalRetentionOfTable fails.
  // ASSERT_OK(
  //     catalog_manager_->WaitForAlterTableToFinish(table_id_1, CoarseMonoClock::Now() +
  //     kDeadline));
  // ASSERT_OK(
  //     catalog_manager_->WaitForAlterTableToFinish(table_id_2, CoarseMonoClock::Now() +
  //     kDeadline));

  // ASSERT_OK(VerifyWalRetentionOfTable(table_id_1));
  // ASSERT_OK(VerifyWalRetentionOfTable(table_id_2));
}

TEST_F(XClusterOutboundReplicationGroupTest, Repair) {
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  auto resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 2);

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupRemoveTable(
          xcluster::ReplicationGroupId("BadId"), table_id_1),
      "xClusterOutboundReplicationGroup BadId not found");

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupRemoveTable(
          kReplicationGroupId, "BadId"),
      "Table BadId not found in xClusterOutboundReplicationGroup");

  ASSERT_OK(XClusterClient().RepairOutboundXClusterReplicationGroupRemoveTable(
      kReplicationGroupId, table_id_1));

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupRemoveTable(
          kReplicationGroupId, table_id_1),
      "not found in xClusterOutboundReplicationGroup");

  resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 1);
  ASSERT_EQ(resp.table_infos(0).table_id(), table_id_2);
  const auto table2_stream_id =
      ASSERT_RESULT(xrepl::StreamId::FromString(resp.table_infos(0).xrepl_stream_id()));

  ASSERT_NOK_STR_CONTAINS(
      GetXClusterStreams(kReplicationGroupId, namespace_id_, {kTableName1}, {kPgSchemaName}),
      "not found in xClusterOutboundReplicationGroup");

  const auto new_stream_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), client_, {table_id_1}));
  ASSERT_EQ(new_stream_ids.size(), 1);
  const auto& new_stream_id = new_stream_ids.front();

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
          xcluster::ReplicationGroupId("BadId"), table_id_1, new_stream_id),
      "xClusterOutboundReplicationGroup BadId not found");

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
          kReplicationGroupId, "BadId", new_stream_id),
      "Table with identifier BadId not found");

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
          kReplicationGroupId, table_id_1, xrepl::StreamId::GenerateRandom()),
      "not found");

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
          kReplicationGroupId, table_id_1, table2_stream_id),
      "belongs to a different table");

  ASSERT_OK(XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
      kReplicationGroupId, table_id_1, new_stream_id));

  ASSERT_NOK_STR_CONTAINS(
      XClusterClient().RepairOutboundXClusterReplicationGroupAddTable(
          kReplicationGroupId, table_id_1, new_stream_id),
      "already exists in");

  resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 2);
  ASSERT_EQ(resp.table_infos(0).table_id(), table_id_2);
  for (const auto& table_info : resp.table_infos()) {
    auto stream_id_str = new_stream_id.ToString();
    if (table_info.table_id() == table_id_2) {
      stream_id_str = table2_stream_id.ToString();
    }
    ASSERT_EQ(table_info.xrepl_stream_id(), stream_id_str);
  }
}

TEST_F(XClusterOutboundReplicationGroupTest, RepairWithYbAdmin) {
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName2));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  auto resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 2);

  ASSERT_OK(CallAdmin(
      producer_cluster(), "repair_xcluster_outbound_replication_remove_table", kReplicationGroupId,
      table_id_1));

  resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 1);
  ASSERT_EQ(resp.table_infos(0).table_id(), table_id_2);

  const auto new_stream_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), client_, {table_id_1}));
  ASSERT_EQ(new_stream_ids.size(), 1);
  const auto& new_stream_id = new_stream_ids.front();

  ASSERT_OK(CallAdmin(
      producer_cluster(), "repair_xcluster_outbound_replication_add_table", kReplicationGroupId,
      table_id_1, new_stream_id.ToString()));

  resp = ASSERT_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_EQ(resp.table_infos_size(), 2);
}

// Validate the GetXClusterOutboundReplicationGroupInfo, and
// GetXClusterOutboundReplicationGroups RPCs.
TEST_F(XClusterOutboundReplicationGroupTest, TestListAPIs) {
  // Create two DBs with different table counts.
  ASSERT_OK(CreateYsqlTable(kNamespaceName, kTableName1));

  const NamespaceId namespace_name_2 = "db2";
  const auto namespace_id_2 = ASSERT_RESULT(CreateYsqlNamespace(namespace_name_2));
  ASSERT_OK(CreateYsqlTable(namespace_name_2, kTableName1));
  ASSERT_OK(CreateYsqlTable(namespace_name_2, "table_2"));

  // Replication group 1 with two namespaces.
  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(
      kReplicationGroupId, {namespace_id_, namespace_id_2}));
  // Wait for checkpointing to complete.
  ASSERT_OK(GetXClusterStreams(kReplicationGroupId, namespace_id_));
  ASSERT_OK(GetXClusterStreams(kReplicationGroupId, namespace_id_2));
  {
    auto group_info = ASSERT_RESULT(
        XClusterClient().GetXClusterOutboundReplicationGroupInfo(kReplicationGroupId));
    ASSERT_EQ(group_info.size(), 2);
    ASSERT_TRUE(group_info.contains(namespace_id_));
    ASSERT_TRUE(group_info.contains(namespace_id_2));
    ASSERT_EQ(group_info[namespace_id_].size(), 1);
    ASSERT_EQ(group_info[namespace_id_2].size(), 2);
  }

  // Replication group 2 with one namespace.
  const xcluster::ReplicationGroupId replication_group2("rg2");
  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(replication_group2, {namespace_id_}));
  ASSERT_OK(GetXClusterStreams(replication_group2, namespace_id_));
  // Wait for checkpointing to complete.
  {
    auto group_info =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroupInfo(replication_group2));
    ASSERT_EQ(group_info.size(), 1);
    ASSERT_TRUE(group_info.contains(namespace_id_));
    ASSERT_EQ(group_info[namespace_id_].size(), 1);
  }

  // List groups for a namespace without any replication groups.
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups("NA"));
    ASSERT_EQ(replication_groups.size(), 0);
  }

  // List all Outbound groups.
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups());
    ASSERT_EQ(replication_groups.size(), 2);
    std::unordered_set<xcluster::ReplicationGroupId> replication_groups_set(
        replication_groups.begin(), replication_groups.end());
    ASSERT_TRUE(replication_groups_set.contains(kReplicationGroupId));
    ASSERT_TRUE(replication_groups_set.contains(replication_group2));
  }

  // List outbound Group per Namespace.
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups(namespace_id_));
    ASSERT_EQ(replication_groups.size(), 2);
    std::unordered_set<xcluster::ReplicationGroupId> replication_groups_set(
        replication_groups.begin(), replication_groups.end());
    ASSERT_TRUE(replication_groups_set.contains(kReplicationGroupId));
    ASSERT_TRUE(replication_groups_set.contains(replication_group2));
  }
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups(namespace_id_2));
    ASSERT_EQ(replication_groups.size(), 1);
    ASSERT_EQ(replication_groups[0], kReplicationGroupId);
  }

  // Delete the first replication group.
  ASSERT_OK(XClusterClient().DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/""));

  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups());
    ASSERT_EQ(replication_groups.size(), 1);
    ASSERT_EQ(replication_groups[0], replication_group2);
  }
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups(namespace_id_));
    ASSERT_EQ(replication_groups.size(), 1);
    ASSERT_EQ(replication_groups[0], replication_group2);
  }
  {
    auto replication_groups =
        ASSERT_RESULT(XClusterClient().GetXClusterOutboundReplicationGroups(namespace_id_2));
    ASSERT_EQ(replication_groups.size(), 0);
  }
}

// Make sure we cleanup the streams of the failed table create.
TEST_F(XClusterOutboundReplicationGroupTest, CleanupStreamsOfFailedTableCreate) {
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));
  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));
  int expected_stream_count = 1;

  auto check_streams = [&]() -> Status {
    auto resp = VERIFY_RESULT(GetXClusterStreams(kReplicationGroupId, namespace_id_));
    SCHECK_EQ(
        resp.table_infos_size(), expected_stream_count, IllegalState,
        Format("Unexpected table infos: $0", resp.ShortDebugString()));
    return Status::OK();
  };

  ASSERT_OK(check_streams());

  auto conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(kNamespaceName));

  // This fails due to GUC yb_test_fail_next_ddl.
  ASSERT_NOK(conn.Execute("SET yb_test_fail_next_ddl=true; CREATE TABLE tbl1 (a int)"));
  ASSERT_OK(check_streams());

  ASSERT_OK(conn.Execute("CREATE TABLE moneyp (a money) PARTITION BY LIST (a);"));
  ++expected_stream_count;
  ASSERT_OK(check_streams());
  // This fails due to invalid cast but still creates (and drops) a new table.
  ASSERT_NOK(conn.Execute("CREATE TABLE moneyp_10 PARTITION OF moneyp FOR VALUES IN (10);"));
  ASSERT_OK(check_streams());

  ASSERT_OK(conn.Execute("CREATE TABLE bigintp (a bigint) PARTITION BY LIST (a);"));
  ++expected_stream_count;
  ASSERT_OK(conn.Execute("CREATE TABLE bigintp_10 PARTITION OF bigintp FOR VALUES IN (10);"));
  ++expected_stream_count;
  ASSERT_OK(check_streams());
  // This fails due to overlap.
  ASSERT_NOK(conn.Execute("CREATE TABLE bigintp_10_2 PARTITION OF bigintp FOR VALUES IN ('10');"));
  ASSERT_OK(check_streams());
}

TEST_F(XClusterOutboundReplicationGroupTest, TestGetStreamByTableId) {
  auto table_id_1 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));

  ASSERT_OK(XClusterClient().CreateOutboundReplicationGroup(kReplicationGroupId, {namespace_id_}));

  // Delete the table to put it into HIDDEN state.
  ASSERT_OK(DropYsqlTable(&producer_cluster_, kNamespaceName, kPgSchemaName, kTableName1));

  // Recreate another table with the same name.
  auto table_id_2 = ASSERT_RESULT(CreateYsqlTable(kNamespaceName, kTableName1));

  // Verify that we can request each table by its table id.
  for (const auto& table_id : {table_id_1, table_id_2}) {
    auto ns_info =
        ASSERT_RESULT(GetXClusterStreamsByTableId(kReplicationGroupId, namespace_id_, {table_id}));
    ASSERT_EQ(ns_info.table_infos_size(), 1);
    ASSERT_EQ(ns_info.table_infos(0).table_id(), table_id);
  }

  // Also verify that we can request both tables by their table ids.
  auto ns_info = ASSERT_RESULT(
      GetXClusterStreamsByTableId(kReplicationGroupId, namespace_id_, {table_id_1, table_id_2}));
  ASSERT_EQ(ns_info.table_infos_size(), 2);
  // Tables should be returned in the same order as requested.
  ASSERT_EQ(ns_info.table_infos(0).table_id(), table_id_1);
  ASSERT_EQ(ns_info.table_infos(1).table_id(), table_id_2);

  // Verify that we can request a table that does not exist.
  ASSERT_NOK_STR_CONTAINS(
      GetXClusterStreamsByTableId(kReplicationGroupId, namespace_id_, {"bad_table_id"}),
      "Table bad_table_id not found");
}

}  // namespace master
}  // namespace yb
