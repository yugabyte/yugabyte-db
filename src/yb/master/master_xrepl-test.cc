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

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include "yb/cdc/cdc_service.h"

#include "yb/cdc/cdc_state_table.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_defaults.h"

#include "yb/master/master-test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_bool(disable_truncate_table);
DECLARE_bool(TEST_ysql_yb_enable_replication_commands);

namespace yb {
namespace master {
constexpr const char* kNamespaceName = "cdc_namespace";
constexpr const char* kNamespaceName2 = "cdc_namespace2";
constexpr const char* kPgsqlNamespaceId = "00004000000030008000000000000000";
constexpr const char* kPgsqlNamespaceId2 = "00004000000030008000000000000010";
constexpr const char* kTableName = "cdc_table";
constexpr int num_tables = 3;
// Keep in sorted order for easier comparison.
constexpr const char* kTableIds[num_tables] = {
    "00004000000030008000000000004001", "00004000000030008000000000004010",
    "00004000000030008000000000004020"};
constexpr const char* kPgReplicationSlotName = "cdc_replication_slot";
constexpr const char* kPgReplicationSlotName2 = "cdc_replication_slot2";
static const Schema kTableSchema({
    ColumnSchema("key", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
    ColumnSchema("v1", DataType::UINT64),
    ColumnSchema("v2", DataType::STRING) });

class MasterTestXRepl  : public MasterTestBase {
 protected:
  void SetUp() override {
    MasterTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_yb_enable_replication_commands) = true;
  }

  Result<xrepl::StreamId> CreateCDCStream(const TableId& table_id);
  Result<xrepl::StreamId> CreateCDCStreamForNamespace(
      const std::string& namespace_name, const std::string& cdcsdk_ysql_replication_slot_name);
  Result<GetCDCStreamResponsePB> GetCDCStream(const xrepl::StreamId& stream_id);
  Result<GetCDCStreamResponsePB> GetCDCStream(const std::string& cdcsdk_ysql_replication_slot_name);
  Status DeleteCDCStream(const xrepl::StreamId& stream_id);
  Result<ListCDCStreamsResponsePB> ListCDCStreams();
  Result<ListCDCStreamsResponsePB> ListCDCSDKStreams();
  Result<bool> IsObjectPartOfXRepl(const TableId& table_id);

  Status SetupUniverseReplication(
      const std::string& producer_id, const std::vector<std::string>& master_addr,
      const std::vector<std::string>& tables);
  Status DeleteUniverseReplication(const std::string& producer_id);
  Result<GetUniverseReplicationResponsePB> GetUniverseReplication(const std::string& producer_id);

};

Result<xrepl::StreamId> MasterTestXRepl::CreateCDCStream(const TableId& table_id) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  req.set_table_id(table_id);
  RETURN_NOT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }

  RETURN_NOT_OK(WaitFor([&](){
    IsCreateTableDoneRequestPB is_create_req;
    IsCreateTableDoneResponsePB is_create_resp;

    is_create_req.mutable_table()->set_table_name(cdc::kCdcStateTableName);
    is_create_req.mutable_table()->mutable_namespace_()->set_name(master::kSystemNamespaceName);

    auto s = proxy_ddl_->IsCreateTableDone(is_create_req, &is_create_resp, ResetAndGetController());
    if (!s.ok()) {
      return false;
    }
    return true;
  }, MonoDelta::FromSeconds(30), "Wait for cdc_state table creation to finish"));

  return xrepl::StreamId::FromString(resp.stream_id());
}

void AddKeyValueToCreateCDCStreamRequestOption(
    CreateCDCStreamRequestPB* req, const std::string& key, const std::string& value) {
  auto new_option = req->add_options();
  new_option->set_key(key);
  new_option->set_value(value);
}

Result<xrepl::StreamId> MasterTestXRepl::CreateCDCStreamForNamespace(
    const std::string& namespace_id, const std::string& cdcsdk_ysql_replication_slot_name) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  req.set_namespace_id(namespace_id);
  req.set_cdcsdk_ysql_replication_slot_name(cdcsdk_ysql_replication_slot_name);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));

  RETURN_NOT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }

  RETURN_NOT_OK(WaitFor(
      [&]() {
        IsCreateTableDoneRequestPB is_create_req;
        IsCreateTableDoneResponsePB is_create_resp;

        is_create_req.mutable_table()->set_table_name(cdc::kCdcStateTableName);
        is_create_req.mutable_table()->mutable_namespace_()->set_name(master::kSystemNamespaceName);

        auto s =
            proxy_ddl_->IsCreateTableDone(is_create_req, &is_create_resp, ResetAndGetController());
        if (!s.ok()) {
          return false;
        }
        return true;
      },
      MonoDelta::FromSeconds(30), "Wait for cdc_state table creation to finish"));

  return xrepl::StreamId::FromString(resp.stream_id());
}

Result<GetCDCStreamResponsePB> MasterTestXRepl::GetCDCStream(
    const xrepl::StreamId& stream_id) {
  GetCDCStreamRequestPB req;
  GetCDCStreamResponsePB resp;
  req.set_stream_id(stream_id.ToString());

  RETURN_NOT_OK(proxy_replication_->GetCDCStream(req, &resp, ResetAndGetController()));
  return resp;
}

Result<GetCDCStreamResponsePB> MasterTestXRepl::GetCDCStream(
    const std::string& cdcsdk_ysql_replication_slot_name) {
  GetCDCStreamRequestPB req;
  GetCDCStreamResponsePB resp;
  req.set_cdcsdk_ysql_replication_slot_name(cdcsdk_ysql_replication_slot_name);

  RETURN_NOT_OK(proxy_replication_->GetCDCStream(req, &resp, ResetAndGetController()));
  return resp;
}

Status MasterTestXRepl::DeleteCDCStream(const xrepl::StreamId& stream_id) {
  DeleteCDCStreamRequestPB req;
  DeleteCDCStreamResponsePB resp;
  req.add_stream_id(stream_id.ToString());

  RETURN_NOT_OK(proxy_replication_->DeleteCDCStream(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Result<ListCDCStreamsResponsePB> MasterTestXRepl::ListCDCStreams() {
  ListCDCStreamsRequestPB req;
  ListCDCStreamsResponsePB resp;

  RETURN_NOT_OK(proxy_replication_->ListCDCStreams(req, &resp, ResetAndGetController()));
  return resp;
}

Result<ListCDCStreamsResponsePB> MasterTestXRepl::ListCDCSDKStreams() {
  ListCDCStreamsRequestPB req;
  ListCDCStreamsResponsePB resp;

  req.set_id_type(IdTypePB::NAMESPACE_ID);

  RETURN_NOT_OK(proxy_replication_->ListCDCStreams(req, &resp, ResetAndGetController()));
  return resp;
}

Result<bool> MasterTestXRepl::IsObjectPartOfXRepl(const TableId& table_id) {
  IsObjectPartOfXReplRequestPB req;
  IsObjectPartOfXReplResponsePB resp;

  req.set_table_id(table_id);
  RETURN_NOT_OK(proxy_replication_->IsObjectPartOfXRepl(req, &resp, ResetAndGetController()));
  return resp.has_error() ? StatusFromPB(resp.error().status()) :
      Result<bool>(resp.is_object_part_of_xrepl());
}

Status MasterTestXRepl::SetupUniverseReplication(
    const std::string& producer_id, const std::vector<std::string>& producer_master_addrs,
    const std::vector<TableId>& tables) {
  SetupUniverseReplicationRequestPB req;
  SetupUniverseReplicationResponsePB resp;

  req.set_producer_id(producer_id);
  req.mutable_producer_master_addresses()->Reserve(narrow_cast<int>(producer_master_addrs.size()));
  for (const auto& addr : producer_master_addrs) {
    std::vector<std::string> hp;
    boost::split(hp, addr, boost::is_any_of(":"));
    CHECK_EQ(hp.size(), 2);
    auto* master = req.add_producer_master_addresses();
    master->set_host(hp[0]);
    master->set_port(boost::lexical_cast<uint32_t>(hp[1]));
  }
  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(tables.size()));
  for (const auto& table : tables) {
    req.add_producer_table_ids(table);
  }

  RETURN_NOT_OK(proxy_replication_->SetupUniverseReplication(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Result<GetUniverseReplicationResponsePB> MasterTestXRepl::GetUniverseReplication(
    const std::string& producer_id) {
  GetUniverseReplicationRequestPB req;
  GetUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->GetUniverseReplication(req, &resp, ResetAndGetController()));
  return resp;
}

Status MasterTestXRepl::DeleteUniverseReplication(const std::string& producer_id) {
  DeleteUniverseReplicationRequestPB req;
  DeleteUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->DeleteUniverseReplication(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

TEST_F(MasterTestXRepl, TestDisableTruncation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_truncate_table) = true;
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));
  auto s = TruncateTableById(table_id);
  EXPECT_TRUE(s.IsNotSupported());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamInvalidTable) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  req.set_table_id("invalidid");
  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, TestCreateCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id = ASSERT_RESULT(CreateCDCStream(table_id));

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespace) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id =
      ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);
  ASSERT_EQ(resp.stream().table_id().size(), num_tables);
  for (auto option : resp.stream().options()) {
    if (option.key() == cdc::kStreamState) {
      ASSERT_EQ(option.value(), SysCDCStreamEntryPB_State_Name(SysCDCStreamEntryPB::ACTIVE));
    }
  }

  std::vector<std::string> receivedTableIds;
  for (auto i = 0; i < num_tables; ++i) {
    receivedTableIds.push_back(resp.stream().table_id().Get(i));
  }
  std::sort(receivedTableIds.begin(), receivedTableIds.end());
  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_EQ(receivedTableIds[i], kTableIds[i]);
  }

  auto resp_via_pub_oid = ASSERT_RESULT(GetCDCStream(kPgReplicationSlotName));
  ASSERT_EQ(resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(resp.stream().stream_id(), stream_id.ToString());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceCql) {
  CreateNamespaceResponsePB create_namespace_resp;
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  ASSERT_OK(CreateNamespace(kNamespaceName, YQLDatabase::YQL_DATABASE_CQL, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;

  req.set_namespace_id(ns_id);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));

  auto stream_id = ASSERT_RESULT(xrepl::StreamId::FromString(resp.stream_id()));
  auto get_stream_resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(get_stream_resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(get_stream_resp.stream().table_id().size(), 0);
  for (auto option : get_stream_resp.stream().options()) {
    if (option.key() == cdc::kStreamState) {
      ASSERT_EQ(option.value(), SysCDCStreamEntryPB_State_Name(SysCDCStreamEntryPB::ACTIVE));
    }
  }

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(1, list_resp.streams_size());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceInvalidDuplicationSlotName) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);  // Use the same name again.
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_ALREADY_PRESENT, resp.error().code());
  ASSERT_NE(
      resp.error().status().message().find(
          "CDC stream with the given replication slot name already exists"),
      std::string::npos)
      << resp.error().status().message();

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(1, list_resp.streams_size());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceInvalidIdTypeOption) {
  CreateNamespaceResponsePB create_namespace_resp;
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;

  // Not setting kIdType option, treated as kTableId by default.
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::INVALID_REQUEST, resp.error().code());
  ASSERT_NE(
      resp.error().status().message().find(
          "Invalid id_type in options. Expected to be NAMESPACEID"),
      std::string::npos)
      << resp.error().status().message();

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(0, list_resp.streams_size());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceMissingReplicationSlotName) {
  CreateNamespaceResponsePB create_namespace_resp;
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;

  // Not populating cdcsdk_ysql_replication_slot_name.
  req.set_namespace_id(ns_id);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::INVALID_REQUEST, resp.error().code());
  ASSERT_NE(
      resp.error().status().message().find(
          "cdcsdk_ysql_replication_slot_name is required for YSQL databases"),
      std::string::npos)
      << resp.error().status().message();

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(0, list_resp.streams_size());
}

TEST_F(MasterTestXRepl, TestDeleteCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id = ASSERT_RESULT(CreateCDCStream(table_id));

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  ASSERT_OK(DeleteCDCStream(stream_id));

  resp.Clear();
  resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, TestDeleteTableWithCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id = ASSERT_RESULT(CreateCDCStream(table_id));

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  // Deleting the table will fail since it has a CDC stream attached.
  TableId id;
  ASSERT_NOK(DeleteTableSync(default_namespace_name, kTableName, &id));

  ASSERT_OK(GetCDCStream(stream_id));
}

// Just disabled on sanitizers because it doesn't need to run often. It's just a unit test.
TEST_F(MasterTestXRepl, YB_DISABLE_TEST_IN_SANITIZERS(TestDeleteCDCStreamNoForceDelete)) {
  // #12255.  Added 'force_delete' flag, but only run this check if the client code specifies it.
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  auto stream_id = xrepl::StreamId::Nil();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  // CreateCDCStream, simulating a fully-created XCluster configuration.
  {
    CreateCDCStreamRequestPB req;
    CreateCDCStreamResponsePB resp;

    req.set_table_id(table_id);
    req.set_initial_state(SysCDCStreamEntryPB::ACTIVE);
    auto source_type_option = req.add_options();
    source_type_option->set_key(cdc::kRecordFormat);
    source_type_option->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));
    ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
    if (resp.has_error()) {
      ASSERT_OK(StatusFromPB(resp.error().status()));
    }
    stream_id = ASSERT_RESULT(CreateCDCStream(table_id));
  }

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  // Should succeed because we don't use the 'force_delete' safety check in this API call.
  ASSERT_OK(DeleteCDCStream(stream_id));

  resp.Clear();
  resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, TestListCDCStreams) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id = ASSERT_RESULT(CreateCDCStream(table_id));

  auto resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(1, resp.streams_size());
  ASSERT_EQ(stream_id.ToString(), resp.streams(0).stream_id());
}

TEST_F(MasterTestXRepl, TestListCDCStreamsCDCSDKWithReplicationSlot) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName2, kPgsqlNamespaceId2, &create_namespace_resp));
  auto ns_id2 = create_namespace_resp.id();

  // 2 tables in cdc_namespace and 1 table in cdc_namespace2
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_2", kTableIds[1], kTableSchema));
  ASSERT_OK(CreatePgsqlTable(ns_id2, "cdc_table_3", kTableIds[2], kTableSchema));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));
  auto stream_id2 = ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id2, kPgReplicationSlotName2));

  auto resp = ASSERT_RESULT(ListCDCSDKStreams());
  ASSERT_EQ(2, resp.streams_size());

  std::set<std::string> expected_stream_ids = {stream_id.ToString(), stream_id2.ToString()};
  std::set<std::string> expected_replication_slot_names = {
      kPgReplicationSlotName, kPgReplicationSlotName2};

  std::set<std::string> resp_stream_ids;
  std::set<std::string> resp_replication_slot_names;
  for (const auto& stream : resp.streams()) {
    resp_stream_ids.insert(stream.stream_id());
    resp_replication_slot_names.insert(stream.cdcsdk_ysql_replication_slot_name());
  }
  ASSERT_EQ(expected_stream_ids, resp_stream_ids);
  ASSERT_EQ(expected_replication_slot_names, resp_replication_slot_names);
}

TEST_F(MasterTestXRepl, TestIsObjectPartOfXRepl) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  FLAGS_cdc_state_table_num_tablets = 1;
  ASSERT_RESULT(CreateCDCStream(table_id));
  ASSERT_TRUE(ASSERT_RESULT(IsObjectPartOfXRepl(table_id)));
}

TEST_F(MasterTestXRepl, TestSetupUniverseReplication) {
  std::string producer_id = "producer_universe";
  std::vector<std::string> producer_masters {"127.0.0.1:7100"};
  std::vector<std::string> tables {"some_table_id"};
  // Always fails because we don't have actual producer.
  ASSERT_NOK(SetupUniverseReplication(producer_id, producer_masters, tables));

  auto resp = ASSERT_RESULT(GetUniverseReplication(producer_id));
  ASSERT_EQ(resp.entry().producer_id(), producer_id);

  ASSERT_EQ(resp.entry().producer_master_addresses_size(), 1);
  std::string addr;
  const auto& hp = resp.entry().producer_master_addresses(0);
  addr = hp.host() + ":" + std::to_string(hp.port());
  ASSERT_EQ(addr, "127.0.0.1:7100");

  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), "some_table_id");
}

TEST_F(MasterTestXRepl, TestDeleteUniverseReplication) {
  std::string producer_id = "producer_universe";
  std::vector<std::string> producer_masters {"127.0.0.1:7100"};
  std::vector<std::string> tables {"some_table_id"};
  // Always fails because we don't have actual producer.
  ASSERT_NOK(SetupUniverseReplication(producer_id, producer_masters, tables));

  // Verify that universe was created.
  auto resp = ASSERT_RESULT(GetUniverseReplication(producer_id));
  ASSERT_EQ(resp.entry().producer_id(), producer_id);

  ASSERT_OK(DeleteUniverseReplication(producer_id));

  resp.Clear();
  resp = ASSERT_RESULT(GetUniverseReplication(producer_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

} // namespace master
} // namespace yb
