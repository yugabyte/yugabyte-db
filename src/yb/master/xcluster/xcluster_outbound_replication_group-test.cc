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

#include "yb/master/xcluster/xcluster_outbound_replication_group.h"

#include <gmock/gmock.h>

#include "yb/client/xcluster_client.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster/xcluster_outbound_replication_group_tasks.h"

#include "yb/rpc/messenger.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_block_xcluster_checkpoint_namespace_task);

using namespace std::placeholders;
using testing::_;
using testing::AtLeast;
using testing::DefaultValue;
using testing::Invoke;
using testing::Return;

namespace yb::master {

const UniverseUuid kTargetUniverseUuid = UniverseUuid::GenerateRandom();
const LeaderEpoch kEpoch = LeaderEpoch(1, 1);

inline bool operator==(const NamespaceCheckpointInfo& lhs, const NamespaceCheckpointInfo& rhs) {
  return YB_STRUCT_EQUALS(initial_bootstrap_required, table_infos);
}

class XClusterRemoteClientMocked : public client::XClusterRemoteClient {
 public:
  XClusterRemoteClientMocked() : client::XClusterRemoteClient("na", MonoDelta::kMax) {
    DefaultValue<Result<UniverseUuid>>::Set(kTargetUniverseUuid);
    DefaultValue<Result<IsOperationDoneResult>>::Set(IsOperationDoneResult::Done());
  }

  MOCK_METHOD2(Init, Status(const xcluster::ReplicationGroupId&, const std::vector<HostPort>&));
  MOCK_METHOD6(
      SetupDbScopedUniverseReplication,
      Result<UniverseUuid>(
          const xcluster::ReplicationGroupId&, const std::vector<HostPort>&,
          const std::vector<NamespaceName>&, const std::vector<NamespaceId>&,
          const std::vector<TableId>&, const std::vector<xrepl::StreamId>&));

  MOCK_METHOD1(
      IsSetupUniverseReplicationDone,
      Result<IsOperationDoneResult>(const xcluster::ReplicationGroupId&));

  MOCK_METHOD6(
      AddNamespaceToDbScopedUniverseReplication,
      Status(
          const xcluster::ReplicationGroupId& replication_group_id,
          const UniverseUuid& target_universe_uuid, const NamespaceName& namespace_name,
          const NamespaceId& source_namespace_id, const std::vector<TableId>& source_table_ids,
          const std::vector<xrepl::StreamId>& bootstrap_ids));
};

Status ValidateEpoch(const LeaderEpoch& epoch) {
  SCHECK_EQ(epoch, kEpoch, IllegalState, "Epoch does not match");
  return Status::OK();
}

class XClusterOutboundReplicationGroupTaskFactoryMocked
    : public XClusterOutboundReplicationGroupTaskFactory {
 public:
  XClusterOutboundReplicationGroupTaskFactoryMocked(
      ThreadPool& async_task_pool, rpc::Messenger& messenger)
      : XClusterOutboundReplicationGroupTaskFactory(
            std::bind(&ValidateEpoch, _1), async_task_pool, messenger) {}
};

class XClusterOutboundReplicationGroupMocked : public XClusterOutboundReplicationGroup {
 public:
  explicit XClusterOutboundReplicationGroupMocked(
      const xcluster::ReplicationGroupId& replication_group_id, HelperFunctions helper_functions,
      XClusterOutboundReplicationGroupTaskFactoryMocked& task_factory)
      : XClusterOutboundReplicationGroup(
            replication_group_id, {}, std::move(helper_functions), /*tasks_tracker=*/nullptr,
            task_factory) {
    remote_client_ = std::make_shared<XClusterRemoteClientMocked>();
  }

  void SetRemoteClient(std::shared_ptr<XClusterRemoteClientMocked> remote_client) {
    remote_client_ = remote_client;
  }

  bool IsDeleted() const {
    SharedLock m_l(mutex_);
    return outbound_rg_info_->LockForRead()->pb.state() ==
           SysXClusterOutboundReplicationGroupEntryPB::DELETED;
  }

  Status AddTable(const TableInfoPtr& table_info, const LeaderEpoch& epoch) {
    // Same as AddTableToXClusterSourceTask.

    RETURN_NOT_OK(CreateStreamForNewTable(table_info->namespace_id(), table_info->id(), epoch));

    Synchronizer sync;
    RETURN_NOT_OK(CheckpointNewTable(
        table_info->namespace_id(), table_info->id(), epoch, sync.AsStdStatusCallback()));
    RETURN_NOT_OK(sync.Wait());

    return MarkNewTablesAsCheckpointed(table_info->namespace_id(), table_info->id(), epoch);
  }

  Status WaitForCheckpoint(const NamespaceId& namespace_id, MonoDelta delta) {
    return LoggedWaitFor(
        [this, namespace_id]() -> Result<bool> {
          return VERIFY_RESULT(GetNamespaceCheckpointInfo(namespace_id)).has_value();
        },
        delta, "Waiting for namespace checkpoint");
  }

  Status AddNamespaceSync(
      const LeaderEpoch& epoch, const NamespaceId& namespace_id, MonoDelta delta) {
    RETURN_NOT_OK(AddNamespace(epoch, namespace_id));
    return WaitForCheckpoint(namespace_id, delta);
  }

  Status AddNamespacesSync(
      const LeaderEpoch& epoch, const std::vector<NamespaceId>& namespace_ids, MonoDelta delta) {
    RETURN_NOT_OK(AddNamespaces(epoch, namespace_ids));
    for (const auto& namespace_id : namespace_ids) {
      RETURN_NOT_OK(LoggedWaitFor(
          [this, namespace_id]() -> Result<bool> {
            return VERIFY_RESULT(GetNamespaceCheckpointInfo(namespace_id)).has_value();
          },
          delta, "Waiting for namespace checkpoint"));
    }

    return Status::OK();
  }

 private:
  virtual Result<std::shared_ptr<client::XClusterRemoteClient>> GetRemoteClient(
      const std::vector<HostPort>& remote_masters) const override {
    return remote_client_;
  }

  std::shared_ptr<XClusterRemoteClientMocked> remote_client_;
};

class XClusterOutboundReplicationGroupMockedTest : public YBTest {
 public:
  const NamespaceName kNamespaceName = "db1";
  const NamespaceId kNamespaceId = "db1_id";
  const PgSchemaName kPgSchemaName = "public", kPgSchemaName2 = "public2";
  const xcluster::ReplicationGroupId kReplicationGroupId = xcluster::ReplicationGroupId("rg1");
  const TableName kTableName1 = "table1", kTableName2 = "table2";
  const TableId kTableId1 = "table_id_1", kTableId2 = "table_id_2";
  const MonoDelta kTimeout = MonoDelta::FromSeconds(5* kTimeMultiplier);

  XClusterOutboundReplicationGroupMockedTest() {
    google::SetVLOGLevel("*", 4);

    ThreadPoolBuilder thread_pool_builder("Test");
    CHECK_OK(thread_pool_builder.Build(&thread_pool));

    rpc::MessengerBuilder messenger_builder("Test");
    messenger = CHECK_RESULT(messenger_builder.Build());

    task_factory = std::make_unique<XClusterOutboundReplicationGroupTaskFactoryMocked>(
        *thread_pool, *messenger);

    CreateNamespace(kNamespaceName, kNamespaceId);
  }

  ~XClusterOutboundReplicationGroupMockedTest() {
    if (thread_pool) {
      thread_pool->Shutdown();
    }
    if (messenger) {
      messenger->Shutdown();
    }
  }

  void CreateNamespace(const NamespaceName& namespace_name, const NamespaceId& namespace_id) {
    scoped_refptr<NamespaceInfo> ns = new NamespaceInfo(namespace_id, /*tasks_tracker=*/nullptr);
    auto l = ns->LockForWrite();
    auto& pb = l.mutable_data()->pb;
    pb.set_name(namespace_name);
    pb.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    l.Commit();
    namespace_infos[namespace_id] = std::move(ns);
  }

  TableInfoPtr CreateTable(
      const NamespaceId& namespace_id, const TableId& table_id, const TableName& table_name,
      const PgSchemaName& pg_schema_name) {
    auto table_info = TableInfoPtr(new TableInfo(table_id, /*colocated=*/false));
    auto l = table_info->LockForWrite();
    auto& pb = l.mutable_data()->pb;
    pb.set_name(table_name);
    pb.set_namespace_id(namespace_id);
    pb.mutable_schema()->set_pgschema_name(pg_schema_name);
    pb.set_table_type(PGSQL_TABLE_TYPE);
    l.Commit();

    namespace_tables[namespace_id].push_back(table_info);
    return table_info;
  }

  void DropTable(const NamespaceId& namespace_id, const TableId& table_id) {
    auto it = std::find_if(
        namespace_tables[namespace_id].begin(), namespace_tables[namespace_id].end(),
        [&table_id](const auto& table_info) { return table_info->id() == table_id; });

    if (it != namespace_tables[namespace_id].end()) {
      namespace_tables[namespace_id].erase(it);
    }
  }

  std::shared_ptr<XClusterOutboundReplicationGroupMocked> CreateReplicationGroup() {
    return std::make_shared<XClusterOutboundReplicationGroupMocked>(
        kReplicationGroupId, helper_functions, *task_factory);
  }

  scoped_refptr<CDCStreamInfo> CreateXClusterStream(const TableId& table_id) {
    auto stream_id = xrepl::StreamId::GenerateRandom();
    xcluster_streams.insert(stream_id);
    return make_scoped_refptr<CDCStreamInfo>(stream_id);
  }

  std::unordered_map<NamespaceId, std::vector<TableInfoPtr>> namespace_tables;
  std::unordered_map<NamespaceId, scoped_refptr<NamespaceInfo>> namespace_infos;
  std::unordered_set<xrepl::StreamId> xcluster_streams;
  std::unique_ptr<ThreadPool> thread_pool;
  std::unique_ptr<rpc::Messenger> messenger;
  std::unique_ptr<XClusterOutboundReplicationGroupTaskFactoryMocked> task_factory;

  XClusterOutboundReplicationGroup::HelperFunctions helper_functions = {
      .get_namespace_func =
          std::bind(&XClusterOutboundReplicationGroupMockedTest::GetNamespace, this, _1),
      .get_tables_func =
          [this](const NamespaceId& namespace_id) { return namespace_tables[namespace_id]; },
      .create_xcluster_streams_func =
          [this](const std::vector<TableId>& table_ids, const LeaderEpoch&) {
            auto create_context = std::make_unique<XClusterCreateStreamsContext>();
            for (const auto& table_id : table_ids) {
              create_context->streams_.emplace_back(CreateXClusterStream(table_id));
            }
            return create_context;
          },
      .checkpoint_xcluster_streams_func =
          [](const std::vector<std::pair<TableId, xrepl::StreamId>>&, StreamCheckpointLocation,
             const LeaderEpoch&, bool, std::function<void(Result<bool>)> user_callback) {
            user_callback(false);  // No bootstrap required.
            return Status::OK();
          },
      .delete_cdc_stream_func = [this](const DeleteCDCStreamRequestPB& req, const LeaderEpoch&)
          -> Result<DeleteCDCStreamResponsePB> {
        DeleteCDCStreamResponsePB resp;
        for (const auto& stream_id_str : req.stream_id()) {
          auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
          SCHECK(xcluster_streams.contains(stream_id), InternalError, "Stream not found");
          xcluster_streams.erase(stream_id);
        }
        return resp;
      },
      .upsert_to_sys_catalog_func =
          [](const LeaderEpoch&, XClusterOutboundReplicationGroupInfo*,
             const std::vector<scoped_refptr<CDCStreamInfo>>&) { return Status::OK(); },
      .delete_from_sys_catalog_func =
          [](const LeaderEpoch&, XClusterOutboundReplicationGroupInfo*) { return Status::OK(); },
  };

  Result<scoped_refptr<NamespaceInfo>> GetNamespace(const NamespaceIdentifierPB& ns_identifier) {
    scoped_refptr<NamespaceInfo> ns_info;
    if (ns_identifier.has_id()) {
      ns_info = FindPtrOrNull(namespace_infos, ns_identifier.id());
    } else {
      for (const auto& [_, namespace_info] : namespace_infos) {
        if (namespace_info->name() == ns_identifier.name()) {
          ns_info = namespace_info;
          break;
        }
      }
    }

    SCHECK(ns_info, NotFound, "Namespace not found", ns_identifier.DebugString());
    return ns_info;
  }

  void VerifyNamespaceCheckpointInfo(
      const TableId& table_id1, const TableId& table_id2, const NamespaceCheckpointInfo& ns_info,
      bool skip_schema_name_check = false) {
    ASSERT_FALSE(ns_info.initial_bootstrap_required);
    ASSERT_EQ(ns_info.table_infos.size(), 2);
    std::set<TableId> table_ids;
    for (const auto& table_info : ns_info.table_infos) {
      if (table_info.table_name == kTableName1) {
        ASSERT_EQ(table_info.table_id, table_id1);
      } else if (table_info.table_name == kTableName2) {
        ASSERT_EQ(table_info.table_id, table_id2);
      } else {
        FAIL() << "Unexpected table name: " << table_info.table_name;
      }
      if (skip_schema_name_check) {
        // Make sure it is not empty.
        ASSERT_FALSE(table_info.pg_schema_name.empty());
      } else {
        ASSERT_EQ(table_info.pg_schema_name, kPgSchemaName);
      }
      ASSERT_FALSE(table_info.stream_id.IsNil());
      ASSERT_TRUE(xcluster_streams.contains(table_info.stream_id));

      table_ids.insert(table_info.table_id);
    }
    ASSERT_TRUE(table_ids.contains(table_id1));
    ASSERT_TRUE(table_ids.contains(table_id2));
  }
};

TEST_F(XClusterOutboundReplicationGroupMockedTest, TestMultipleTable) {
  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);
  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2);
  auto outbound_rg_ptr = CreateReplicationGroup();
  auto& outbound_rg = *outbound_rg_ptr;

  ASSERT_FALSE(outbound_rg.HasNamespace(kNamespaceId));
  ASSERT_OK(outbound_rg.AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));
  ASSERT_TRUE(outbound_rg.HasNamespace(kNamespaceId));

  auto ns_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info_opt.has_value());

  // We should have 2 streams now.
  ASSERT_EQ(xcluster_streams.size(), 2);

  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      kTableId1, kTableId2, *ns_info_opt, /*skip_schema_name_check=*/true));
  for (const auto& table_info : ns_info_opt->table_infos) {
    // Order is not deterministic so search with the table name.
    if (table_info.table_name == kTableName1) {
      ASSERT_EQ(table_info.pg_schema_name, kPgSchemaName);
    } else {
      ASSERT_EQ(table_info.pg_schema_name, kPgSchemaName2);
    }
  }

  // Get the table info in a custom order.
  ns_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(
      kNamespaceId, {{kTableName2, kPgSchemaName2}, {kTableName1, kPgSchemaName}}));
  ASSERT_TRUE(ns_info_opt.has_value());

  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      kTableId1, kTableId2, *ns_info_opt, /*skip_schema_name_check=*/true));
  ASSERT_EQ(ns_info_opt->table_infos[0].pg_schema_name, kPgSchemaName2);
  ASSERT_EQ(ns_info_opt->table_infos[1].pg_schema_name, kPgSchemaName);
  ASSERT_EQ(ns_info_opt->table_infos[0].table_name, kTableName2);
  ASSERT_EQ(ns_info_opt->table_infos[1].table_name, kTableName1);

  ASSERT_OK(outbound_rg.Delete(/*target_master_addresses=*/{}, kEpoch));
  ASSERT_FALSE(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  auto result = outbound_rg.GetMetadata();
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsNotFound());
  ASSERT_TRUE(outbound_rg.IsDeleted());

  // We should have 0 streams now.
  ASSERT_TRUE(xcluster_streams.empty());
}

TEST_F(XClusterOutboundReplicationGroupMockedTest, AddDeleteNamespaces) {
  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);
  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = true;

  const NamespaceName namespace_name_2 = "db2";
  const NamespaceId namespace_id_2 = "ns_id_2";
  const TableId ns2_table_id_1 = "ns2_table_id_1", ns2_table_id_2 = "ns2_table_id_2";
  CreateNamespace(namespace_name_2, namespace_id_2);
  CreateTable(namespace_id_2, ns2_table_id_1, kTableName1, kPgSchemaName);
  CreateTable(namespace_id_2, ns2_table_id_2, kTableName2, kPgSchemaName);

  auto outbound_rg_ptr = CreateReplicationGroup();
  auto& outbound_rg = *outbound_rg_ptr;
  ASSERT_OK(outbound_rg.AddNamespaces(kEpoch, {kNamespaceId}));

  // Adding second namespace while the first operation has not completed should fail.
  ASSERT_NOK_STR_CONTAINS(
      outbound_rg.AddNamespace(kEpoch, namespace_id_2), "has in progress tasks");

  // Removing the namespace should succeed and cause the task to complete.
  ASSERT_OK(outbound_rg.RemoveNamespace(kEpoch, kNamespaceId, /*target_master_addresses=*/{}));
  ASSERT_FALSE(outbound_rg.HasNamespace(kNamespaceId));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = false;

  ASSERT_OK(LoggedWaitFor(
      [&outbound_rg]() { return !outbound_rg.HasTasks(); }, kTimeout,
      "Waiting for tasks to complete"));
  ASSERT_EQ(xcluster_streams.size(), 0);

  ASSERT_OK(outbound_rg.AddNamespacesSync(kEpoch, {kNamespaceId}, kTimeout));

  // We should have 2 streams now.
  ASSERT_EQ(xcluster_streams.size(), 2);
  auto xcluster_streams_initial = xcluster_streams;

  // Make sure invalid namespace id is handled correctly.
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo("BadId"));

  // Make sure only the namespace that was added is returned.
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(namespace_id_2));

  auto ns1_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns1_info_opt.has_value());
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(kTableId1, kTableId2, *ns1_info_opt));

  // Add the second namespace.
  ASSERT_OK(outbound_rg.AddNamespaceSync(kEpoch, namespace_id_2, kTimeout));

  // We should have 4 streams now.
  ASSERT_EQ(xcluster_streams.size(), 4);

  // The info of the first namespace should not change.
  auto ns1_info_dup = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns1_info_opt.has_value());
  ASSERT_EQ(*ns1_info_dup, *ns1_info_opt);

  // Validate the seconds namespace.
  auto ns2_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(namespace_id_2));
  ASSERT_TRUE(ns2_info_opt.has_value());
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(ns2_table_id_1, ns2_table_id_2, *ns2_info_opt));

  ASSERT_OK(outbound_rg.RemoveNamespace(kEpoch, kNamespaceId, /*target_master_addresses=*/{}));
  ASSERT_FALSE(outbound_rg.HasNamespace(kNamespaceId));
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));

  // We should only have only the streams from second namespace.
  ASSERT_EQ(xcluster_streams.size(), 2);

  // new_xcluster_streams and all_xcluster_streams should not overlap.
  for (const auto& stream : xcluster_streams) {
    ASSERT_FALSE(xcluster_streams_initial.contains(stream));
  }

  // Delete the group while there is a checkpoint task in progress.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = true;
  ASSERT_OK(outbound_rg.AddNamespaces(kEpoch, {kNamespaceId}));

  ASSERT_OK(outbound_rg.Delete(/*target_master_addresses=*/{}, kEpoch));
  ASSERT_FALSE(outbound_rg.HasNamespace(kNamespaceId));
  ASSERT_FALSE(outbound_rg.HasNamespace(namespace_id_2));
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(namespace_id_2));
  ASSERT_TRUE(xcluster_streams.empty());
}

TEST_F(XClusterOutboundReplicationGroupMockedTest, CreateTargetReplicationGroup) {
  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);

  auto outbound_rg_ptr = CreateReplicationGroup();
  auto& outbound_rg = *outbound_rg_ptr;
  auto remote_client = std::make_shared<XClusterRemoteClientMocked>();
  outbound_rg.SetRemoteClient(remote_client);

  ASSERT_OK(outbound_rg.AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));

  std::vector<xrepl::StreamId> streams{xcluster_streams.begin(), xcluster_streams.end()};
  EXPECT_CALL(
      *remote_client,
      SetupDbScopedUniverseReplication(
          kReplicationGroupId, _, std::vector<NamespaceName>{kNamespaceName},
          std::vector<NamespaceId>{kNamespaceId}, std::vector<TableId>{kTableId1}, streams))
      .Times(AtLeast(1));

  ASSERT_OK(outbound_rg.CreateXClusterReplication({}, {}, kEpoch));

  EXPECT_CALL(*remote_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::NotDone()));

  auto create_result = ASSERT_RESULT(outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch));
  ASSERT_FALSE(create_result.done());

  // Fail the Setup.
  const auto error_str = "Failed by test";
  EXPECT_CALL(*remote_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(STATUS(IllegalState, error_str)));
  auto result = outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), error_str);

  auto pb = ASSERT_RESULT(outbound_rg.GetMetadata());
  ASSERT_TRUE(pb.has_target_universe_info());
  ASSERT_EQ(pb.target_universe_info().universe_uuid(), kTargetUniverseUuid.ToString());
  ASSERT_EQ(
      pb.target_universe_info().state(),
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfoPB::CREATING_REPLICATION_GROUP);

  EXPECT_CALL(*remote_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::Done(STATUS(IllegalState, error_str))));
  create_result = ASSERT_RESULT(outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch));
  ASSERT_TRUE(create_result.done());
  ASSERT_STR_CONTAINS(create_result.status().ToString(), error_str);

  pb = ASSERT_RESULT(outbound_rg.GetMetadata());
  ASSERT_FALSE(pb.has_target_universe_info());

  // Success case.
  EXPECT_CALL(*remote_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::Done()));

  EXPECT_CALL(*remote_client, SetupDbScopedUniverseReplication(_, _, _, _, _, _));

  // Calling create again should not do anything.
  ASSERT_OK(outbound_rg.CreateXClusterReplication({}, {}, kEpoch));
  create_result = ASSERT_RESULT(outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch));
  ASSERT_TRUE(create_result.done());
  ASSERT_OK(create_result.status());

  pb = ASSERT_RESULT(outbound_rg.GetMetadata());
  ASSERT_TRUE(pb.has_target_universe_info());
  ASSERT_EQ(pb.target_universe_info().universe_uuid(), kTargetUniverseUuid.ToString());
  ASSERT_EQ(
      pb.target_universe_info().state(),
      SysXClusterOutboundReplicationGroupEntryPB::TargetUniverseInfoPB::REPLICATING);
}

TEST_F(XClusterOutboundReplicationGroupMockedTest, AddTable) {
  auto table_info1 = CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);
  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2);

  auto outbound_rg = CreateReplicationGroup();

  ASSERT_OK(outbound_rg->AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));
  ASSERT_TRUE(outbound_rg->HasNamespace(kNamespaceId));
  ASSERT_EQ(xcluster_streams.size(), 2);

  auto ns_info = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_EQ(ns_info->table_infos.size(), 2);

  // Same table should not get added twice.
  ASSERT_OK(outbound_rg->AddTable(table_info1, kEpoch));

  ASSERT_EQ(ns_info->table_infos.size(), 2);

  const TableName table_3 = "table3";
  const TableId table_id_3 = "table_id_3";
  auto table_info3 = CreateTable(kNamespaceId, table_id_3, table_3, kPgSchemaName);

  ASSERT_OK(outbound_rg->AddTable(table_info3, kEpoch));

  ASSERT_EQ(xcluster_streams.size(), 3);
  ns_info = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info.has_value());
  ASSERT_EQ(ns_info->table_infos.size(), 3);
}

// If we create a table during checkpoint, it should fail.
TEST_F(XClusterOutboundReplicationGroupMockedTest, AddTableDuringCheckpoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  auto* sync_point_instance = yb::SyncPoint::GetInstance();

  SyncPoint::GetInstance()->LoadDependency(
      {{"TESTAddTableDuringCheckpoint::TableCreated",
        "XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap"}});
  sync_point_instance->EnableProcessing();

  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);

  auto outbound_rg = CreateReplicationGroup();
  ASSERT_OK(outbound_rg->AddNamespace(kEpoch, kNamespaceId));

  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2);
  TEST_SYNC_POINT("TESTAddTableDuringCheckpoint::TableCreated");

  auto status = outbound_rg->WaitForCheckpoint(kNamespaceId, kTimeout);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(),
      "List of tables changed during xCluster checkpoint of replication group "
      "xClusterOutboundReplicationGroup rg1: [table_id_2]");

  sync_point_instance->DisableProcessing();
}

// If we drop a table during checkpoint, it should fail.
TEST_F(XClusterOutboundReplicationGroupMockedTest, DropTableDuringCheckpoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  auto* sync_point_instance = yb::SyncPoint::GetInstance();

  SyncPoint::GetInstance()->LoadDependency(
      {{"TESTAddTableDuringCheckpoint::TableCreated",
        "XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap"}});
  sync_point_instance->EnableProcessing();

  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);
  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2);

  auto outbound_rg = CreateReplicationGroup();
  ASSERT_OK(outbound_rg->AddNamespace(kEpoch, kNamespaceId));

  DropTable(kNamespaceId, kTableId1);
  TEST_SYNC_POINT("TESTAddTableDuringCheckpoint::TableCreated");

  auto status = outbound_rg->WaitForCheckpoint(kNamespaceId, kTimeout);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(),
      "List of tables changed during xCluster checkpoint of replication group "
      "xClusterOutboundReplicationGroup rg1: [table_id_1]");

  sync_point_instance->DisableProcessing();
}

}  // namespace yb::master
