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

#include "yb/client/xcluster_client_mock.h"

#include "yb/common/xcluster_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/xcluster/add_table_to_xcluster_source_task.h"
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
DECLARE_bool(xcluster_enable_ddl_replication);

using namespace std::chrono_literals;
using namespace std::placeholders;
using testing::_;
using testing::AtLeast;
using testing::DefaultValue;
using testing::Invoke;
using testing::Return;

namespace yb::master {

const auto kEpoch = master::LeaderEpoch(1, 1);

inline bool operator==(const NamespaceCheckpointInfo& lhs, const NamespaceCheckpointInfo& rhs) {
  return YB_STRUCT_EQUALS(initial_bootstrap_required, table_infos);
}

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
      const xcluster::ReplicationGroupId& replication_group_id,
      const SysXClusterOutboundReplicationGroupEntryPB& outbound_replication_group_pb,
      HelperFunctions helper_functions,
      XClusterOutboundReplicationGroupTaskFactoryMocked& task_factory)
      : XClusterOutboundReplicationGroup(
            replication_group_id, outbound_replication_group_pb, std::move(helper_functions),
            /*tasks_tracker=*/nullptr, task_factory) {
    remote_client_ = std::make_shared<client::MockXClusterRemoteClientHolder>();
  }

  client::MockXClusterClient& GetMockXClusterClient() {
    return remote_client_->GetMockXClusterClient();
  }

  bool IsDeleted() const {
    SharedLock m_l(mutex_);
    return outbound_rg_info_->LockForRead()->pb.state() ==
           SysXClusterOutboundReplicationGroupEntryPB::DELETED;
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
  virtual Result<std::shared_ptr<client::XClusterRemoteClientHolder>> GetRemoteClient(
      const std::vector<HostPort>& remote_masters) const override {
    return remote_client_;
  }

  std::shared_ptr<client::MockXClusterRemoteClientHolder> remote_client_;
};

const UniverseUuid kTargetUniverseUuid = UniverseUuid::GenerateRandom();
const NamespaceName kNamespaceName = "db1";
const NamespaceId kNamespaceId = "db1_id";
const PgSchemaName kPgSchemaName = "public", kPgSchemaName2 = "public2";
const xcluster::ReplicationGroupId kReplicationGroupId = xcluster::ReplicationGroupId("rg1");
const TableName kTableName1 = "table1", kTableName2 = "table2";
const TableId kTableId1 = "table_id_1", kTableId2 = "table_id_2";
const MonoDelta kTimeout = 5s * kTimeMultiplier;

class XClusterOutboundReplicationGroupMockedTest : public YBTest {
 public:
  XClusterOutboundReplicationGroupMockedTest() {
    google::SetVLOGLevel("*", 4);

    DefaultValue<Result<UniverseUuid>>::Set(kTargetUniverseUuid);
    DefaultValue<Result<IsOperationDoneResult>>::Set(IsOperationDoneResult::Done());

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

  void SetUp() override {
    TEST_SETUP_SUPER(YBTest);
    LOG(INFO) << "Test uses automatic mode: " << UseAutomaticMode();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_enable_ddl_replication) = UseAutomaticMode();
  }

  virtual bool UseAutomaticMode() {
    // Except for parameterized tests, we currently default to semi-automatic mode.
    return false;
  }

  // How many extra streams/tables a namespace has
  int OverheadStreamsCount() {
    if (!UseAutomaticMode()) {
      return 0;
    }
    // Automatic DDL mode involves 2 extra tables: sequences_data and
    // yb_xcluster_ddl_replication.dd_queue.
    return 2;
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

  // The actual AddTableToXClusterSourceTask requires a CatalogManager so directly invoke the
  // required methods.
  Status AddTableToXClusterSourceTask(const master::TableInfo& table) {
    for (const auto& outbound_replication_group : outbound_replication_groups_) {
      if (!outbound_replication_group->HasNamespace(table.namespace_id())) {
        continue;
      }
      RETURN_NOT_OK(outbound_replication_group->CreateStreamForNewTable(
          table.namespace_id(), table.id(), kEpoch));
      Synchronizer sync;
      RETURN_NOT_OK(outbound_replication_group->CheckpointNewTable(
          table.namespace_id(), table.id(), kEpoch, sync.AsStdStatusCallback()));
      RETURN_NOT_OK(sync.Wait());
      RETURN_NOT_OK(outbound_replication_group->MarkNewTablesAsCheckpointed(
          table.namespace_id(), table.id(), kEpoch));
    }

    return Status::OK();
  }

  bool TableExists(const NamespaceId& namespace_id, const TableId& table_id) EXCLUDES(mutex_) {
    SharedLock l(mutex_);
    return std::any_of(
        namespace_tables[namespace_id].begin(), namespace_tables[namespace_id].end(),
        [&table_id](const auto& table_info) { return table_info->id() == table_id; });
  }

  Status CreateTableIfNotExists(
      const NamespaceId& namespace_id, const TableId& table_id, const TableName& table_name,
      const PgSchemaName& pg_schema_name) EXCLUDES(mutex_) {
    if (!TableExists(namespace_id, table_id)) {
      RETURN_NOT_OK(CreateTable(namespace_id, table_id, table_name, pg_schema_name));
    }
    return Status::OK();
  }

  Result<TableInfoPtr> CreateTable(
      const NamespaceId& namespace_id, const TableId& table_id, const TableName& table_name,
      const PgSchemaName& pg_schema_name) EXCLUDES(mutex_) {
    SCHECK_FORMAT(
        !TableExists(namespace_id, table_id), AlreadyPresent,
        "Table $0 already exists in namespace $1", table_id, namespace_id);

    auto table_info = TableInfoPtr(new TableInfo(table_id, /*colocated=*/false));
    {
      auto l = table_info->LockForWrite();
      auto& pb = l.mutable_data()->pb;
      pb.set_state(master::SysTablesEntryPB::PREPARING);
      pb.set_name(table_name);
      pb.set_namespace_id(namespace_id);
      pb.mutable_schema()->set_deprecated_pgschema_name(pg_schema_name);
      pb.set_table_type(PGSQL_TABLE_TYPE);
      l.Commit();
    }

    {
      std::lock_guard l2(mutex_);
      namespace_tables[namespace_id].push_back(table_info);
    }

    if (IsTableEligibleForXClusterReplication(*table_info, UseAutomaticMode())) {
      RETURN_NOT_OK(AddTableToXClusterSourceTask(*table_info));
    }

    {
      auto l = table_info->LockForWrite();
      l.mutable_data()->pb.set_state(master::SysTablesEntryPB::RUNNING);
      l.Commit();
    }

    return table_info;
  }

  void DropTable(const NamespaceId& namespace_id, const TableId& table_id) {
    std::lock_guard l(mutex_);
    auto it = std::find_if(
        namespace_tables[namespace_id].begin(), namespace_tables[namespace_id].end(),
        [&table_id](const auto& table_info) { return table_info->id() == table_id; });

    if (it != namespace_tables[namespace_id].end()) {
      namespace_tables[namespace_id].erase(it);
    }
  }

  std::shared_ptr<XClusterOutboundReplicationGroupMocked> CreateReplicationGroup() {
    SysXClusterOutboundReplicationGroupEntryPB outbound_replication_group_pb{};
    outbound_replication_group_pb.set_automatic_ddl_mode(UseAutomaticMode());
    auto group = std::make_shared<XClusterOutboundReplicationGroupMocked>(
        kReplicationGroupId, outbound_replication_group_pb, helper_functions, *task_factory);
    outbound_replication_groups_.push_back(group);
    return group;
  }

  scoped_refptr<CDCStreamInfo> CreateXClusterStream(const TableId& table_id) {
    auto stream_id = xrepl::StreamId::GenerateRandom();
    xcluster_streams.insert(stream_id);
    return make_scoped_refptr<CDCStreamInfo>(stream_id);
  }

  mutable std::shared_mutex mutex_;
  std::unordered_map<NamespaceId, std::vector<TableInfoPtr>> namespace_tables GUARDED_BY(mutex_);
  std::unordered_map<NamespaceId, scoped_refptr<NamespaceInfo>> namespace_infos;
  std::unordered_set<xrepl::StreamId> xcluster_streams;
  std::unique_ptr<ThreadPool> thread_pool;
  std::unique_ptr<rpc::Messenger> messenger;
  std::unique_ptr<XClusterOutboundReplicationGroupTaskFactoryMocked> task_factory;
  std::vector<std::shared_ptr<XClusterOutboundReplicationGroupMocked>> outbound_replication_groups_;

  XClusterOutboundReplicationGroup::HelperFunctions helper_functions = {
      .create_sequences_data_table_func = [this]() -> Status {
        EXPECT_TRUE(UseAutomaticMode());
        RETURN_NOT_OK(CreateTableIfNotExists(
            kPgSequencesDataNamespaceId, kPgSequencesDataTableId, "sequences_data", ""));

        return Status::OK();
      },
      .advance_oid_counters_func =
          [](const NamespaceId& namespace_id) -> Status { return Status::OK(); },
      .get_normal_oid_higher_than_any_used_normal_oid_func =
          [](const NamespaceId& namespace_id) -> Result<uint32_t> { return 100'000; },
      .get_namespace_func =
          std::bind(&XClusterOutboundReplicationGroupMockedTest::GetNamespace, this, _1),
      .get_tables_func =
          [this](const NamespaceId& namespace_id, bool include_sequences_data) {
            std::vector<TableInfoPtr> tables;
            {
              SharedLock l(mutex_);
              tables = namespace_tables[namespace_id];
            }
            std::vector<TableDesignator> table_designators;
            for (const auto& table_info : tables) {
              if (IsTableEligibleForXClusterReplication(*table_info, UseAutomaticMode())) {
                table_designators.emplace_back(table_info);
              }
            }
            if (include_sequences_data) {
              std::vector<TableInfoPtr> sequences_tables;
              {
                SharedLock l(mutex_);
                sequences_tables = namespace_tables[kPgSequencesDataNamespaceId];
              }
              if (sequences_tables.size() > 0) {
                table_designators.emplace_back(TableDesignator::CreateSequenceTableDesignator(
                    sequences_tables.front(), namespace_id));
              }
            }
            return table_designators;
          },
      .is_automatic_mode_switchover_func = [](const NamespaceId&) { return false; },
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
      .setup_ddl_replication_extension_func =
          [this](const NamespaceId& namespace_id, StdStatusCallback callback) -> Status {
        EXPECT_TRUE(UseAutomaticMode());
        for (const auto& table_name :
             {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
          RETURN_NOT_OK(CreateTableIfNotExists(
              namespace_id, /*table_id=*/table_name, table_name, xcluster::kDDLQueuePgSchemaName));
        }

        callback(Status::OK());
        return Status::OK();
      },
      .drop_ddl_replication_extension_func =
          [this](
              const NamespaceId& namespace_id,
              const xcluster::ReplicationGroupId& drop_replication_group_id) -> Status {
        SCHECK(UseAutomaticMode(), InternalError, "Should only be called in automatic mode");
        for (const auto& table_name :
             {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
          DropTable(namespace_id, table_name);
        }
        return Status::OK();
      },
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
      bool all_tables_included = true, const PgSchemaName& table2_schema_name = kPgSchemaName) {
    EXPECT_EQ(ns_info.initial_bootstrap_required, UseAutomaticMode());
    ASSERT_EQ(ns_info.table_infos.size(), 2 + (all_tables_included ? OverheadStreamsCount() : 0));
    std::set<TableId> table_ids;
    for (const auto& table_info : ns_info.table_infos) {
      SCOPED_TRACE("table name: " + table_info.table_name);
      if (table_info.table_name == kTableName1) {
        ASSERT_EQ(table_info.table_id, table_id1);
        EXPECT_EQ(table_info.pg_schema_name, kPgSchemaName);
      } else if (table_info.table_name == kTableName2) {
        ASSERT_EQ(table_info.table_id, table_id2);
        EXPECT_EQ(table_info.pg_schema_name, table2_schema_name);
      } else if (table_info.table_name == "sequences_data") {
        ASSERT_TRUE(all_tables_included);
        ASSERT_TRUE(xcluster::IsSequencesDataAlias(table_info.table_id));
        EXPECT_TRUE(table_info.pg_schema_name.empty());
      } else if (
          table_info.table_name == xcluster::kDDLQueueTableName &&
          table_info.pg_schema_name == xcluster::kDDLQueuePgSchemaName) {
        ASSERT_TRUE(all_tables_included);
      } else {
        FAIL() << "Unexpected table name: " << table_info.table_name;
      }

      EXPECT_FALSE(table_info.stream_id.IsNil());
      EXPECT_TRUE(xcluster_streams.contains(table_info.stream_id));

      table_ids.insert(table_info.table_id);
    }
    EXPECT_TRUE(table_ids.contains(table_id1));
    EXPECT_TRUE(table_ids.contains(table_id2));
  }

  Status VerifyExtensionTablesDeleted(const NamespaceId namespace_id) {
    if (!UseAutomaticMode()) {
      return Status::OK();
    }
    for (const auto& table_name :
         {xcluster::kDDLQueueTableName, xcluster::kDDLReplicatedTableName}) {
      if (TableExists(namespace_id, table_name)) {
        return STATUS_FORMAT(
            InternalError, "Table $0 still exists in namespace $1", table_name, namespace_id);
      }
    }
    return Status::OK();
  }
};

class XClusterOutboundReplicationGroupMockedParameterized
    : public XClusterOutboundReplicationGroupMockedTest,
      public ::testing::WithParamInterface<bool> {
 public:
  bool UseAutomaticMode() override { return GetParam(); }
};

INSTANTIATE_TEST_CASE_P(
    AutoMode, XClusterOutboundReplicationGroupMockedParameterized, ::testing::Values(true));
INSTANTIATE_TEST_CASE_P(
    SemiMode, XClusterOutboundReplicationGroupMockedParameterized, ::testing::Values(false));

TEST_P(XClusterOutboundReplicationGroupMockedParameterized, TestMultipleTable) {
  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));
  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2));
  auto outbound_rg_ptr = CreateReplicationGroup();
  auto& outbound_rg = *outbound_rg_ptr;

  ASSERT_FALSE(outbound_rg.HasNamespace(kNamespaceId));
  ASSERT_OK(outbound_rg.AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));
  ASSERT_TRUE(outbound_rg.HasNamespace(kNamespaceId));

  auto ns_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info_opt.has_value());

  // We should have 2 streams for normal tables now.
  ASSERT_EQ(xcluster_streams.size(), 2 + OverheadStreamsCount());

  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      kTableId1, kTableId2, *ns_info_opt, /*all_tables_included=*/true, kPgSchemaName2));

  // Get the table info in a custom order.
  ns_info_opt = ASSERT_RESULT(outbound_rg.GetNamespaceCheckpointInfo(
      kNamespaceId, {{kTableName2, kPgSchemaName2}, {kTableName1, kPgSchemaName}}));
  ASSERT_TRUE(ns_info_opt.has_value());

  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(
      kTableId1, kTableId2, *ns_info_opt, /*all_tables_included=*/false, kPgSchemaName2));
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
  ASSERT_OK(VerifyExtensionTablesDeleted(kNamespaceId));

  // We should have 0 streams now.
  ASSERT_TRUE(xcluster_streams.empty());
}

TEST_P(XClusterOutboundReplicationGroupMockedParameterized, AddDeleteNamespaces) {
  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));
  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_block_xcluster_checkpoint_namespace_task) = true;

  const NamespaceName namespace_name_2 = "db2";
  const NamespaceId namespace_id_2 = "ns_id_2";
  const TableId ns2_table_id_1 = "ns2_table_id_1", ns2_table_id_2 = "ns2_table_id_2";
  CreateNamespace(namespace_name_2, namespace_id_2);
  ASSERT_OK(CreateTable(namespace_id_2, ns2_table_id_1, kTableName1, kPgSchemaName));
  ASSERT_OK(CreateTable(namespace_id_2, ns2_table_id_2, kTableName2, kPgSchemaName));

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

  // We should have 2 normal streams now.
  EXPECT_EQ(xcluster_streams.size(), 2 + OverheadStreamsCount());
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

  // We should have 4 normal streams now.
  ASSERT_EQ(xcluster_streams.size(), 4 + 2 * OverheadStreamsCount());

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
  ASSERT_EQ(xcluster_streams.size(), 2 + OverheadStreamsCount());

  // New_xcluster_streams and all_xcluster_streams should not overlap.
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
  ASSERT_OK(VerifyExtensionTablesDeleted(kNamespaceId));
}

TEST_P(XClusterOutboundReplicationGroupMockedParameterized, CreateTargetReplicationGroup) {
  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));

  auto outbound_rg_ptr = CreateReplicationGroup();
  auto& outbound_rg = *outbound_rg_ptr;
  auto& xcluster_client = outbound_rg.GetMockXClusterClient();

  ASSERT_OK(outbound_rg.AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));

  std::vector<xrepl::StreamId> expected_streams{xcluster_streams.begin(), xcluster_streams.end()};
  std::vector<TableId> expected_tables{kTableId1};
  if (UseAutomaticMode()) {
    expected_tables.push_back(xcluster::GetSequencesDataAliasForNamespace(kNamespaceId));
    expected_tables.push_back(xcluster::kDDLQueueTableName);
  }
  EXPECT_CALL(
      xcluster_client,
      SetupDbScopedUniverseReplication(
          kReplicationGroupId, _, std::vector<NamespaceName>{kNamespaceName},
          std::vector<NamespaceId>{kNamespaceId},
          ::testing::UnorderedElementsAreArray(expected_tables),
          ::testing::UnorderedElementsAreArray(expected_streams), UseAutomaticMode()))
      .Times(AtLeast(1));

  ASSERT_OK(outbound_rg.CreateXClusterReplication({}, {}, kEpoch));

  EXPECT_CALL(xcluster_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::NotDone()));

  auto create_result = ASSERT_RESULT(outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch));
  ASSERT_FALSE(create_result.done());

  // Fail the Setup.
  const auto error_str = "Failed by test";
  EXPECT_CALL(xcluster_client, IsSetupUniverseReplicationDone(_))
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

  EXPECT_CALL(xcluster_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::Done(STATUS(IllegalState, error_str))));
  create_result = ASSERT_RESULT(outbound_rg.IsCreateXClusterReplicationDone({}, kEpoch));
  ASSERT_TRUE(create_result.done());
  ASSERT_STR_CONTAINS(create_result.status().ToString(), error_str);

  pb = ASSERT_RESULT(outbound_rg.GetMetadata());
  ASSERT_FALSE(pb.has_target_universe_info());

  // Success case.
  EXPECT_CALL(xcluster_client, IsSetupUniverseReplicationDone(_))
      .WillOnce(Return(IsOperationDoneResult::Done()));

  EXPECT_CALL(xcluster_client, SetupDbScopedUniverseReplication(_, _, _, _, _, _, _));

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

TEST_P(XClusterOutboundReplicationGroupMockedParameterized, AddTable) {
  auto table_info1 =
      ASSERT_RESULT(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));
  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2));

  auto outbound_rg = CreateReplicationGroup();

  ASSERT_OK(outbound_rg->AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));
  ASSERT_TRUE(outbound_rg->HasNamespace(kNamespaceId));
  EXPECT_EQ(xcluster_streams.size(), 2 + OverheadStreamsCount());

  auto ns_info = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  EXPECT_EQ(ns_info->table_infos.size(), 2 + OverheadStreamsCount());

  // Make sure AddTableToXClusterSourceTask is idempotent.
  ASSERT_OK(AddTableToXClusterSourceTask(*table_info1));
  ASSERT_EQ(ns_info->table_infos.size(), 2 + OverheadStreamsCount());

  const TableName table_3 = "table3";
  const TableId table_id_3 = "table_id_3";
  auto table_info3 = ASSERT_RESULT(CreateTable(kNamespaceId, table_id_3, table_3, kPgSchemaName));

  ASSERT_EQ(xcluster_streams.size(), 3 + OverheadStreamsCount());
  ns_info = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info.has_value());
  ASSERT_EQ(ns_info->table_infos.size(), 3 + OverheadStreamsCount());
}

// If we create a table during checkpoint, it should fail.
TEST_F(XClusterOutboundReplicationGroupMockedTest, AddTableDuringCheckpoint) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  auto* sync_point_instance = yb::SyncPoint::GetInstance();

  SyncPoint::GetInstance()->LoadDependency(
      {{.predecessor = "TESTAddTableDuringCheckpoint::TableCreated",
        .successor = "XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap"}});
  sync_point_instance->EnableProcessing();

  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));

  auto outbound_rg = CreateReplicationGroup();
  ASSERT_OK(outbound_rg->AddNamespace(kEpoch, kNamespaceId));

  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2));
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
      {{.predecessor = "TESTAddTableDuringCheckpoint::TableCreated",
        .successor = "XClusterOutboundReplicationGroup::CreateStreamsForInitialBootstrap"}});
  sync_point_instance->EnableProcessing();

  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));
  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName2));

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

// Make newly created tables are automatically checkpointed.
TEST_P(XClusterOutboundReplicationGroupMockedParameterized, AutomaticCheckpointOfNewTables) {
  ASSERT_OK(CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName));
  auto outbound_rg = CreateReplicationGroup();

  ASSERT_OK(outbound_rg->AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));
  ASSERT_TRUE(outbound_rg->HasNamespace(kNamespaceId));

  auto ns_info_opt = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info_opt.has_value());
  ASSERT_EQ(ns_info_opt->table_infos.size(), 1 + OverheadStreamsCount());

  ASSERT_OK(CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName));

  ns_info_opt = ASSERT_RESULT(outbound_rg->GetNamespaceCheckpointInfo(kNamespaceId));
  ASSERT_TRUE(ns_info_opt.has_value());
  ASSERT_NO_FATALS(VerifyNamespaceCheckpointInfo(kTableId1, kTableId2, *ns_info_opt));
}

class XClusterOutboundReplicationGroupMockedAutomaticDDLMode
    : public XClusterOutboundReplicationGroupMockedTest {
 public:
  bool UseAutomaticMode() override { return true; }
};

TEST_F(XClusterOutboundReplicationGroupMockedAutomaticDDLMode, AutoCreateSysTables) {
  auto outbound_rg = CreateReplicationGroup();
  ASSERT_OK(outbound_rg->AddNamespaceSync(kEpoch, kNamespaceId, kTimeout));

  ASSERT_TRUE(TableExists(kPgSequencesDataNamespaceId, kPgSequencesDataTableId));
  ASSERT_TRUE(TableExists(kNamespaceId, /*table_id=*/xcluster::kDDLQueueTableName));
  ASSERT_TRUE(TableExists(kNamespaceId, /*table_id=*/xcluster::kDDLReplicatedTableName));
}

}  // namespace yb::master
