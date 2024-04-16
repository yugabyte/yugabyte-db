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

#include "yb/master/master-test_base.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_bool(disable_truncate_table);
DECLARE_bool(ysql_yb_enable_replication_commands);
DECLARE_bool(ysql_yb_enable_replica_identity);
DECLARE_bool(cdc_enable_postgres_replica_identity);
DECLARE_bool(enable_backfilling_cdc_stream_with_replication_slot);
DECLARE_uint32(max_replication_slots);

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
    // Default of FLAGS_cdc_state_table_num_tablets is to fallback to num_tablet_servers which is 0
    // in this test. So we need to explicitly set it here.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_table_num_tablets) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_backfilling_cdc_stream_with_replication_slot) = true;
  }

  Result<xrepl::StreamId> CreateCDCStream(const TableId& table_id);
  Result<xrepl::StreamId> CreateCDCStreamForNamespace(
      const std::string& namespace_name, const std::string& cdcsdk_ysql_replication_slot_name);
  Result<xrepl::StreamId> CreateCDCStreamForNamespaceOlderVersion(
      const std::string& namespace_name,
      cdc::CDCRecordType record_type = cdc::CDCRecordType::CHANGE);
  Result<GetCDCStreamResponsePB> GetCDCStream(const xrepl::StreamId& stream_id);
  Result<GetCDCStreamResponsePB> GetCDCStream(const std::string& cdcsdk_ysql_replication_slot_name);
  Status DeleteCDCStream(const xrepl::StreamId& stream_id);
  Result<DeleteCDCStreamResponsePB> DeleteCDCStream(
      const std::vector<xrepl::StreamId>& stream_ids,
      const std::vector<std::string>& cdcsdk_ysql_replication_slot_name);
  Result<ListCDCStreamsResponsePB> ListCDCStreams();
  Result<ListCDCStreamsResponsePB> ListCDCSDKStreams();
  Result<bool> IsObjectPartOfXRepl(const TableId& table_id);

  Status SetupUniverseReplication(
      const std::string& producer_id, const std::vector<std::string>& master_addr,
      const std::vector<std::string>& tables);
  Status DeleteUniverseReplication(const std::string& producer_id);
  Result<GetUniverseReplicationResponsePB> GetUniverseReplication(const std::string& producer_id);

  Status CreateTableWithTableId(TableId* table_id);
  Status DeleteNamespace(std::string namespace_id, YQLDatabase db_type);
  Result<bool> IsDeleteNamespaceDone(std::string namespace_id);
  Status WaitForDeleteNamespaceToComplete(
      std::string namespace_id, MonoDelta timeout, const std::string& failure_message);

 private:
  Result<xrepl::StreamId> CreateCDCStreamForNamespace(
      CreateCDCStreamRequestPB* req, cdc::CDCRecordType record_type = cdc::CDCRecordType::CHANGE);
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

  req.set_namespace_id(namespace_id);
  req.set_cdcsdk_ysql_replication_slot_name(cdcsdk_ysql_replication_slot_name);
  return CreateCDCStreamForNamespace(&req);
}

Result<xrepl::StreamId> MasterTestXRepl::CreateCDCStreamForNamespaceOlderVersion(
    const std::string& namespace_id, cdc::CDCRecordType record_type) {
  CreateCDCStreamRequestPB req;

  // In older versions, the namespace_id is passed into the table_id field.
  req.set_table_id(namespace_id);
  return CreateCDCStreamForNamespace(&req, record_type);
}

Result<xrepl::StreamId> MasterTestXRepl::CreateCDCStreamForNamespace(
    CreateCDCStreamRequestPB* req, cdc::CDCRecordType record_type) {
  CreateCDCStreamResponsePB resp;

  AddKeyValueToCreateCDCStreamRequestOption(req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      req, cdc::kRecordType, CDCRecordType_Name(record_type));

  RETURN_NOT_OK(proxy_replication_->CreateCDCStream(*req, &resp, ResetAndGetController()));
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

Result<DeleteCDCStreamResponsePB> MasterTestXRepl::DeleteCDCStream(
    const std::vector<xrepl::StreamId>& stream_ids,
    const std::vector<std::string>& cdcsdk_ysql_replication_slot_names) {
  DeleteCDCStreamRequestPB req;
  DeleteCDCStreamResponsePB resp;
  for (const auto& stream_id : stream_ids) {
    req.add_stream_id(stream_id.ToString());
  }
  for (const auto& replication_slot_name : cdcsdk_ysql_replication_slot_names) {
    req.add_cdcsdk_ysql_replication_slot_name(replication_slot_name);
  }

  RETURN_NOT_OK(proxy_replication_->DeleteCDCStream(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return resp;
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

  req.set_replication_group_id(producer_id);
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
  req.set_replication_group_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->GetUniverseReplication(req, &resp, ResetAndGetController()));
  return resp;
}

Status MasterTestXRepl::DeleteUniverseReplication(const std::string& producer_id) {
  DeleteUniverseReplicationRequestPB req;
  DeleteUniverseReplicationResponsePB resp;
  req.set_replication_group_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->DeleteUniverseReplication(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Status MasterTestXRepl::CreateTableWithTableId(TableId* table_id) {
  return CreateTable(kTableName, kTableSchema, table_id);
}

Status MasterTestXRepl::DeleteNamespace(std::string namespace_id, YQLDatabase db_type) {
  DeleteNamespaceRequestPB req;
  DeleteNamespaceResponsePB resp;
  req.mutable_namespace_()->set_id(namespace_id);
  req.set_database_type(db_type);
  auto s = proxy_ddl_->DeleteNamespace(req, &resp, ResetAndGetController());
  if (!s.ok()) {
    return s;
  }
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Result<bool> MasterTestXRepl::IsDeleteNamespaceDone(std::string namespace_id) {
  IsDeleteNamespaceDoneRequestPB req;
  IsDeleteNamespaceDoneResponsePB resp;
  req.mutable_namespace_()->set_id(std::move(namespace_id));

  auto s = proxy_ddl_->IsDeleteNamespaceDone(req, &resp, ResetAndGetController());
  if (!s.ok()) {
    return s;
  }
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return resp.done();
}

Status MasterTestXRepl::WaitForDeleteNamespaceToComplete(
    std::string namespace_id, MonoDelta timeout, const std::string& failure_message) {
  return WaitFor(
      [this, &namespace_id]() -> Result<bool> { return IsDeleteNamespaceDone(namespace_id); },
      timeout, failure_message);
}

TEST_F(MasterTestXRepl, TestDisableTruncation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_truncate_table) = true;
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));
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
  ASSERT_OK(CreateTableWithTableId(&table_id));

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


  req.set_namespace_id(ns_id);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

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

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;

  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(
      resp.error().status().message().find(
          "Creation of CDCSDK stream with a replication slot name is disallowed"),
      std::string::npos)
      << resp.error().status().message();
}

TEST_F(MasterTestXRepl, YB_DISABLE_TEST_IN_TSAN(TestCDCStreamCreationWithNewRecordTypeDisabled)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_postgres_replica_identity) = false;

  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::PG_DEFAULT));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(
      resp.error().status().message().find("Using new record types is disallowed in the middle of "
                                           "an upgrade. Finalize the upgrade and try again."),
      std::string::npos)
      << resp.error().status().message();
}

// TODO(#19930): Disallow stream creation with older record types once we have disallowed the YSQL
// CDC commands in yb-admin.
TEST_F(MasterTestXRepl, YB_DISABLE_TEST_IN_TSAN(TestCDCStreamCreationWithOldRecordType)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_enable_postgres_replica_identity) = true;

  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::FULL_ROW_NEW_IMAGE));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error());
}

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceInvalidDuplicationSlotName) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);  // Use the same name again.
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

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


  // Not setting kIdType option, treated as kTableId by default.
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

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

TEST_F(MasterTestXRepl, TestCreateCDCStreamForNamespaceLimitReached) {
  // Set max_replication_slots to a small value for ease of testing.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_replication_slots) = 1;

  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));

  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;
  req.set_namespace_id(ns_id);
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName2);
  AddKeyValueToCreateCDCStreamRequestOption(&req, cdc::kIdType, cdc::kNamespaceId);
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kSourceType, CDCRequestSource_Name(cdc::CDCRequestSource::CDCSDK));
  AddKeyValueToCreateCDCStreamRequestOption(
      &req, cdc::kRecordType, CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::REPLICATION_SLOT_LIMIT_REACHED, resp.error().code());
  ASSERT_NE(
      resp.error().status().message().find(
          "Replication slot limit reached"),
      std::string::npos)
      << resp.error().status().message();

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(1, list_resp.streams_size());
}

TEST_F(MasterTestXRepl, TestDeleteCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));

  auto stream_id = ASSERT_RESULT(CreateCDCStream(table_id));

  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  ASSERT_OK(DeleteCDCStream(stream_id));

  resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, TestDeleteCDCStreamWithReplicationSlotName) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));
  auto resp = ASSERT_RESULT(GetCDCStream(stream_id));

  ASSERT_OK(DeleteCDCStream({} /*stream_ids*/, {kPgReplicationSlotName}));

  resp = ASSERT_RESULT(GetCDCStream(stream_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, TestDeleteCDCStreamWithStreamIdAndReplicationSlotName) {
  // Setup two streams - with and without replication slot name.
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));

  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  auto stream_id_1 = ASSERT_RESULT(CreateCDCStream(table_id));
  auto stream_id_2 =
      ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));

  // Streams were created successfully.
  auto resp = ASSERT_RESULT(GetCDCStream(stream_id_1));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  resp = ASSERT_RESULT(GetCDCStream(stream_id_2));
  ASSERT_EQ(resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);

  // Delete streams:
  // 1. Using stream_id
  // 2. Using replication slot name
  auto delete_resp = ASSERT_RESULT(DeleteCDCStream({stream_id_1}, {kPgReplicationSlotName}));

  resp = ASSERT_RESULT(GetCDCStream(stream_id_1));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());

  resp = ASSERT_RESULT(GetCDCStream(stream_id_2));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());

  ASSERT_EQ(delete_resp.not_found_stream_ids_size(), 0);
  ASSERT_EQ(delete_resp.not_found_cdcsdk_ysql_replication_slot_names_size(), 0);
}

TEST_F(MasterTestXRepl, TestDeleteCDCStreamNotFound) {
  DeleteCDCStreamRequestPB req;
  DeleteCDCStreamResponsePB resp;
  req.add_stream_id("00000000000000000000000000000000");
  req.add_cdcsdk_ysql_replication_slot_name("non_existent_replication_slot");

  ASSERT_OK(proxy_replication_->DeleteCDCStream(req, &resp, ResetAndGetController()));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(resp.not_found_stream_ids_size(), 1);
  ASSERT_EQ(resp.not_found_stream_ids().Get(0), "00000000000000000000000000000000");
  ASSERT_EQ(resp.not_found_cdcsdk_ysql_replication_slot_names_size(), 1);
  ASSERT_EQ(
      resp.not_found_cdcsdk_ysql_replication_slot_names().Get(0), "non_existent_replication_slot");
}

TEST_F(MasterTestXRepl, TestDeleteTableWithCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));

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
  ASSERT_OK(CreateTableWithTableId(&table_id));

  auto stream_id = xrepl::StreamId::Nil();
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

TEST_F(MasterTestXRepl, TestCreateDropCDCStreamWithReplicationSlotName) {
  // Default of FLAGS_cdc_state_table_num_tablets is to fallback to num_tablet_servers which is 0.
  // So we need to explicitly set it here.
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }

  // Create and Delete CDC stream with replication slot name in quick succession.
  for (size_t i = 0; i < 2; i++) {
    auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));
    auto resp = ASSERT_RESULT(GetCDCStream(stream_id));
    ASSERT_EQ(resp.stream().namespace_id(), ns_id);
    ASSERT_EQ(resp.stream().stream_id(), stream_id.ToString());
    ASSERT_EQ(resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);

    ASSERT_OK(DeleteCDCStream({} /*stream_ids*/, {kPgReplicationSlotName}));
    resp = ASSERT_RESULT(GetCDCStream(kPgReplicationSlotName));
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
  }

  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));
  // Wait for background task to run (length of two wait intervals).
  SleepFor(MonoDelta::FromMilliseconds(2 * FLAGS_catalog_manager_bg_task_wait_ms));

  // Ensure that the created stream wasn't deleted from the cdcsdk_replication_slots_to_stream_map_
  // map.
  auto resp = ASSERT_RESULT(GetCDCStream(kPgReplicationSlotName));
  ASSERT_EQ(resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(resp.stream().stream_id(), stream_id.ToString());
  ASSERT_EQ(resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);
}

TEST_F(MasterTestXRepl, TestListCDCStreams) {
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));

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

TEST_F(MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStream) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  // Disable replication commands and replica identity and create a CDCSDK stream to simulate the
  // scenario of the stream being created on the older version.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;
  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespaceOlderVersion(ns_id));

  // Enable replication commands and replica identity and add the replication slot name to the
  // created stream.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;

  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  req.set_stream_id(stream_id.ToString());
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));
  ASSERT_FALSE(resp.has_error());

  auto list_resp = ASSERT_RESULT(ListCDCStreams());
  ASSERT_EQ(1, list_resp.streams_size());
  ASSERT_EQ(stream_id.ToString(), list_resp.streams(0).stream_id());
  ASSERT_EQ(kPgReplicationSlotName, list_resp.streams(0).cdcsdk_ysql_replication_slot_name());

  // The stream should be searchable using the replication slot name as well.
  auto get_resp = ASSERT_RESULT(GetCDCStream(kPgReplicationSlotName));
  ASSERT_EQ(get_resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(get_resp.stream().stream_id(), stream_id.ToString());
  ASSERT_EQ(get_resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);
  ASSERT_EQ(get_resp.stream().replica_identity_map_size(), 1);
  for (auto [table_id, replica_identity] : get_resp.stream().replica_identity_map()) {
    ASSERT_EQ(replica_identity, PgReplicaIdentity::CHANGE);
  }

  // Updating the replication slot name of the stream again should fail.
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName2);
  req.set_stream_id(stream_id.ToString());
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));
  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(
      resp.error().status().message().find(
          "Cannot update the replication slot name of a CDCSDK stream"),
      std::string::npos)
      << resp.error().status().message();
}

TEST_F(MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStreamMissingStreamId) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));

  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(
      resp.error().status().message().find(
          "Both CDC Stream ID and Replication slot name must be provided"),
      std::string::npos)
      << resp.error().status().message();
}

TEST_F(
    MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStreamMissingReplicationSlotName) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  // Disable replication commands and replica identity and create a CDCSDK stream to simulate the
  // scenario of the stream being created on the older version.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;
  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespaceOlderVersion(ns_id));

  // Enable replication commands and replica identity and add the replication slot name to the
  // created stream.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_stream_id(stream_id.ToString());
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));

  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(
      resp.error().status().message().find(
          "Both CDC Stream ID and Replication slot name must be provided"),
      std::string::npos)
      << resp.error().status().message();
}

TEST_F(MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStreamInvalidStreamId) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);

  // Stream with this id doesn't exist.
  req.set_stream_id("00004000000030008000000000004001");
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));

  ASSERT_TRUE(resp.has_error());
  ASSERT_NE(resp.error().status().message().find("Could not find CDC stream"), std::string::npos)
      << resp.error().status().message();
}

TEST_F(MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStreamInvalidSlotNames) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  // Disable replication commands and replica identity and create a CDCSDK stream to simulate the
  // scenario of the stream being created on the older version.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;
  auto stream_id = ASSERT_RESULT(CreateCDCStreamForNamespaceOlderVersion(ns_id));

  // Enable replication commands and replica identity and add the replication slot name to the
  // created stream.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_stream_id(stream_id.ToString());

  auto invalid_character_message =
      "Replication slot names may only contain lower case letters, numbers, and the underscore "
      "character.";
  std::vector<std::pair<std::string, std::string>> slot_name_expected_error = {
      std::make_pair("", "Replication slot name cannot be empty"),
      std::make_pair(std::string('a', 64), "Replication slot name length must be < 64"),
      std::make_pair("pgA", invalid_character_message),
      std::make_pair("abc#", invalid_character_message)};
  for (const auto& test_case : slot_name_expected_error) {
    req.set_cdcsdk_ysql_replication_slot_name(test_case.first);

    ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
        req, &resp, ResetAndGetController()));

    ASSERT_TRUE(resp.has_error());
    ASSERT_NE(resp.error().status().message().find(test_case.second), std::string::npos)
        << resp.error().status().message();
  }
}

TEST_F(MasterTestXRepl, TestYsqlBackfillReplicationSlotNameToCDCSDKStreamInvalidReplicaIdentity) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();
  ASSERT_OK(CreatePgsqlTable(ns_id, "cdc_table_1", kTableIds[0], kTableSchema));

  // Disable replication commands and replica identity and create a CDCSDK stream to simulate the
  // scenario of the stream being created on the older version.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = false;
  auto stream_id = ASSERT_RESULT(
      CreateCDCStreamForNamespaceOlderVersion(ns_id, cdc::CDCRecordType::FULL_ROW_NEW_IMAGE));

  // Enable replication commands and replica identity and add the replication slot name to the
  // created stream.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_commands) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replica_identity) = true;

  YsqlBackfillReplicationSlotNameToCDCSDKStreamRequestPB req;
  YsqlBackfillReplicationSlotNameToCDCSDKStreamResponsePB resp;
  req.set_cdcsdk_ysql_replication_slot_name(kPgReplicationSlotName);
  req.set_stream_id(stream_id.ToString());
  ASSERT_OK(proxy_replication_->YsqlBackfillReplicationSlotNameToCDCSDKStream(
      req, &resp, ResetAndGetController()));
  ASSERT_FALSE(resp.has_error());

  auto get_resp = ASSERT_RESULT(GetCDCStream(kPgReplicationSlotName));
  ASSERT_EQ(get_resp.stream().namespace_id(), ns_id);
  ASSERT_EQ(get_resp.stream().stream_id(), stream_id.ToString());
  ASSERT_EQ(get_resp.stream().cdcsdk_ysql_replication_slot_name(), kPgReplicationSlotName);
  ASSERT_EQ(get_resp.stream().replica_identity_map_size(), 1);

  // Since the record type FULL_ROW_NEW_IMAGE does not have a corresponding replica identity, we
  // will get CHANGE as the replica identity for the table.
  for (auto [table_id, replica_identity] : get_resp.stream().replica_identity_map()) {
    ASSERT_EQ(replica_identity, PgReplicaIdentity::CHANGE);
  }
}

TEST_F(MasterTestXRepl, TestIsObjectPartOfXRepl) {
  TableId table_id;
  ASSERT_OK(CreateTableWithTableId(&table_id));

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
  ASSERT_EQ(resp.entry().replication_group_id(), producer_id);

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
  ASSERT_EQ(resp.entry().replication_group_id(), producer_id);

  ASSERT_OK(DeleteUniverseReplication(producer_id));

  resp.Clear();
  resp = ASSERT_RESULT(GetUniverseReplication(producer_id));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestXRepl, DropNamespaceWithLiveCDCStream) {
  CreateNamespaceResponsePB create_namespace_resp;
  ASSERT_OK(CreatePgsqlNamespace(kNamespaceName, kPgsqlNamespaceId, &create_namespace_resp));
  auto ns_id = create_namespace_resp.id();

  for (auto i = 0; i < num_tables; ++i) {
    ASSERT_OK(CreatePgsqlTable(ns_id, Format("cdc_table_$0", i), kTableIds[i], kTableSchema));
  }
  ASSERT_RESULT(CreateCDCStreamForNamespace(ns_id, kPgReplicationSlotName));
  ASSERT_OK(DeleteNamespace(ns_id, YQL_DATABASE_PGSQL));
  ASSERT_OK(WaitForDeleteNamespaceToComplete(
      ns_id, MonoDelta::FromSeconds(30), "Failed waiting for database drop to complete"));
}

} // namespace master
} // namespace yb
