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

#include "yb/master/catalog_entity_info.h"

#include "yb/util/test_util.h"

namespace yb::master {

class XClusterOutboundReplicationGroupMockedTest : public YBTest {
 public:
  const NamespaceName kNamespaceName = "db1";
  const NamespaceId kNamespaceId = "db1_id";
  const PgSchemaName kPgSchemaName = "public", kPgSchemaName2 = "public2";
  const xcluster::ReplicationGroupId kReplicationGroupId = xcluster::ReplicationGroupId("rg1");
  const TableName kTableName1 = "table1", kTableName2 = "table2";
  const TableId kTableId1 = "table_id_1", kTableId2 = "table_id_2";
  const LeaderEpoch kEpoch = LeaderEpoch(1, 1);
  const CoarseTimePoint kDeadline = CoarseTimePoint::max();

  XClusterOutboundReplicationGroupMockedTest() {
    google::SetVLOGLevel("xcluster*", 4);

    CreateNamespace(kNamespaceName, kNamespaceId);
  }

  void CreateNamespace(const NamespaceName& namespace_name, const NamespaceId& namespace_id) {
    namespace_ids[namespace_name] = namespace_id;
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

  XClusterOutboundReplicationGroup CreateReplicationGroup() {
    return XClusterOutboundReplicationGroup(kReplicationGroupId, {}, helper_functions);
  }

  xrepl::StreamId CreateXClusterStream(const TableId& table_id) {
    auto stream_id = xrepl::StreamId::GenerateRandom();
    xcluster_streams.insert(stream_id);
    return stream_id;
  }

  std::unordered_map<NamespaceId, std::vector<TableInfoPtr>> namespace_tables;
  std::unordered_map<NamespaceName, NamespaceId> namespace_ids;
  std::unordered_set<xrepl::StreamId> xcluster_streams;

  XClusterOutboundReplicationGroup::HelperFunctions helper_functions = {
      .get_namespace_id_func =
          [this](YQLDatabase db_type, const NamespaceName& namespace_name) {
            return namespace_ids[namespace_name];
          },
      .get_tables_func =
          [this](const NamespaceId& namespace_id) { return namespace_tables[namespace_id]; },
      .bootstrap_tables_func =
          [this](const std::vector<TableInfoPtr>& table_infos, CoarseTimePoint deadline)
          -> Result<std::vector<xrepl::StreamId>> {
        std::vector<xrepl::StreamId> stream_ids;
        for (const auto& table_info : table_infos) {
          stream_ids.emplace_back(CreateXClusterStream(table_info->id()));
        }
        return stream_ids;
      },
      .delete_cdc_stream_func = [this](
                                    const DeleteCDCStreamRequestPB& req,
                                    const LeaderEpoch& epoch) -> Result<DeleteCDCStreamResponsePB> {
        DeleteCDCStreamResponsePB resp;
        for (const auto& stream_id_str : req.stream_id()) {
          auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(stream_id_str));
          SCHECK(xcluster_streams.contains(stream_id), InternalError, "Stream not found");
          xcluster_streams.erase(stream_id);
        }
        return resp;
      },
      .upsert_to_sys_catalog_func =
          [](const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return Status::OK();
          },
      .delete_from_sys_catalog_func =
          [](const LeaderEpoch& epoch, XClusterOutboundReplicationGroupInfo* info) {
            return Status::OK();
          },
  };

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
  auto outbound_rg = CreateReplicationGroup();

  auto namespace_id = ASSERT_RESULT(outbound_rg.AddNamespace(kEpoch, kNamespaceName, kDeadline));
  ASSERT_EQ(namespace_id, kNamespaceId);

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

  ASSERT_OK(outbound_rg.Delete(kEpoch));
  ASSERT_FALSE(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));

  // We should have 0 streams now.
  ASSERT_TRUE(xcluster_streams.empty());
}

TEST_F(XClusterOutboundReplicationGroupMockedTest, AddDeleteNamespaces) {
  CreateTable(kNamespaceId, kTableId1, kTableName1, kPgSchemaName);
  CreateTable(kNamespaceId, kTableId2, kTableName2, kPgSchemaName);

  const NamespaceName namespace_name_2 = "db2";
  const NamespaceId namespace_id_2 = "ns_id_2";
  const TableId ns2_table_id_1 = "ns2_table_id_1", ns2_table_id_2 = "ns2_table_id_2";
  CreateNamespace(namespace_name_2, namespace_id_2);
  CreateTable(namespace_id_2, ns2_table_id_1, kTableName1, kPgSchemaName);
  CreateTable(namespace_id_2, ns2_table_id_2, kTableName2, kPgSchemaName);

  auto outbound_rg = CreateReplicationGroup();
  auto out_namespace_id =
      ASSERT_RESULT(outbound_rg.AddNamespaces(kEpoch, {kNamespaceName}, kDeadline));
  ASSERT_EQ(out_namespace_id.size(), 1);
  ASSERT_EQ(out_namespace_id[0], kNamespaceId);

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
  auto out_namespace_id2 =
      ASSERT_RESULT(outbound_rg.AddNamespace(kEpoch, namespace_name_2, kDeadline));
  ASSERT_EQ(out_namespace_id2, namespace_id_2);

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

  ASSERT_OK(outbound_rg.RemoveNamespace(kEpoch, kNamespaceId));
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(kNamespaceId));

  // We should only have only the streams from second namespace.
  ASSERT_EQ(xcluster_streams.size(), 2);

  // new_xcluster_streams and all_xcluster_streams should not overlap.
  for (const auto& stream : xcluster_streams) {
    ASSERT_FALSE(xcluster_streams_initial.contains(stream));
  }

  ASSERT_OK(outbound_rg.Delete(kEpoch));
  ASSERT_NOK(outbound_rg.GetNamespaceCheckpointInfo(namespace_id_2));
  ASSERT_TRUE(xcluster_streams.empty());
}

}  // namespace yb::master
