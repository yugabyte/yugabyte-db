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

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/master_defaults.h"

#include "../../src/yb/master/master-test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"

DECLARE_int32(cdc_state_table_num_tablets);
DECLARE_bool(disable_truncate_table);

namespace yb {
namespace master {
namespace enterprise {

constexpr const char* kTableName = "cdc_table";
static const Schema kTableSchema({ ColumnSchema("key", INT32),
                                   ColumnSchema("v1", UINT64),
                                   ColumnSchema("v2", STRING) },
                                 1);

class MasterTestEnt  : public MasterTestBase {
 protected:
  Status CreateCDCStream(const TableId& table_id, CDCStreamId* stream_id);
  Status GetCDCStream(const CDCStreamId& stream_id, GetCDCStreamResponsePB* resp);
  Status DeleteCDCStream(const CDCStreamId& stream_id);
  Status ListCDCStreams(ListCDCStreamsResponsePB* resp);

  Status SetupUniverseReplication(
      const std::string& producer_id, const std::vector<std::string>& master_addr,
      const std::vector<std::string>& tables);
  Status DeleteUniverseReplication(const std::string& producer_id);
  Status GetUniverseReplication(
      const std::string& producer_id, GetUniverseReplicationResponsePB* resp);

};

Status MasterTestEnt::CreateCDCStream(const TableId& table_id, CDCStreamId* stream_id) {
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

    is_create_req.mutable_table()->set_table_name(master::kCdcStateTableName);
    is_create_req.mutable_table()->mutable_namespace_()->set_name(master::kSystemNamespaceName);

    auto s = proxy_ddl_->IsCreateTableDone(is_create_req, &is_create_resp, ResetAndGetController());
    if (!s.ok()) {
      return false;
    }
    return true;
  }, MonoDelta::FromSeconds(30), "Wait for cdc_state table creation to finish"));

  *stream_id = resp.stream_id();
  return Status::OK();
}

Status MasterTestEnt::GetCDCStream(const CDCStreamId& stream_id, GetCDCStreamResponsePB* resp) {
  GetCDCStreamRequestPB req;
  req.set_stream_id(stream_id);

  RETURN_NOT_OK(proxy_replication_->GetCDCStream(req, resp, ResetAndGetController()));
  if (resp->has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp->error().status()));
  }
  return Status::OK();
}

Status MasterTestEnt::DeleteCDCStream(const CDCStreamId& stream_id) {
  DeleteCDCStreamRequestPB req;
  DeleteCDCStreamResponsePB resp;
  req.add_stream_id(stream_id);

  RETURN_NOT_OK(proxy_replication_->DeleteCDCStream(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

Status MasterTestEnt::ListCDCStreams(ListCDCStreamsResponsePB* resp) {
  ListCDCStreamsRequestPB req;

  RETURN_NOT_OK(proxy_replication_->ListCDCStreams(req, resp, ResetAndGetController()));
  if (resp->has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp->error().status()));
  }
  return Status::OK();
}

Status MasterTestEnt::SetupUniverseReplication(
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

Status MasterTestEnt::GetUniverseReplication(
    const std::string& producer_id, GetUniverseReplicationResponsePB* resp) {
  GetUniverseReplicationRequestPB req;
  req.set_producer_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->GetUniverseReplication(req, resp, ResetAndGetController()));
  if (resp->has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp->error().status()));
  }
  return Status::OK();
}

Status MasterTestEnt::DeleteUniverseReplication(const std::string& producer_id) {
  DeleteUniverseReplicationRequestPB req;
  DeleteUniverseReplicationResponsePB resp;
  req.set_producer_id(producer_id);

  RETURN_NOT_OK(proxy_replication_->DeleteUniverseReplication(req, &resp, ResetAndGetController()));
  if (resp.has_error()) {
    RETURN_NOT_OK(StatusFromPB(resp.error().status()));
  }
  return Status::OK();
}

TEST_F(MasterTestEnt, TestDisableTruncation) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_truncate_table) = true;
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));
  auto s = TruncateTableById(table_id);
  EXPECT_TRUE(s.IsNotSupported());
}

TEST_F(MasterTestEnt, TestCreateCDCStreamInvalidTable) {
  CreateCDCStreamRequestPB req;
  CreateCDCStreamResponsePB resp;

  req.set_table_id("invalidid");
  ASSERT_OK(proxy_replication_->CreateCDCStream(req, &resp, ResetAndGetController()));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestEnt, TestCreateCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  CDCStreamId stream_id;
  FLAGS_cdc_state_table_num_tablets = 1;
  ASSERT_OK(CreateCDCStream(table_id, &stream_id));

  GetCDCStreamResponsePB resp;
  ASSERT_OK(GetCDCStream(stream_id, &resp));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);
}

TEST_F(MasterTestEnt, TestDeleteCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  CDCStreamId stream_id;
  FLAGS_cdc_state_table_num_tablets = 1;
  ASSERT_OK(CreateCDCStream(table_id, &stream_id));

  GetCDCStreamResponsePB resp;
  ASSERT_OK(GetCDCStream(stream_id, &resp));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  ASSERT_OK(DeleteCDCStream(stream_id));

  resp.Clear();
  ASSERT_NOK(GetCDCStream(stream_id, &resp));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestEnt, TestDeleteTableWithCDCStream) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  CDCStreamId stream_id;
  FLAGS_cdc_state_table_num_tablets = 1;
  ASSERT_OK(CreateCDCStream(table_id, &stream_id));

  GetCDCStreamResponsePB resp;
  ASSERT_OK(GetCDCStream(stream_id, &resp));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  // Deleting the table will fail since it has a CDC stream attached.
  TableId id;
  ASSERT_NOK(DeleteTableSync(default_namespace_name, kTableName, &id));

  ASSERT_OK(GetCDCStream(stream_id, &resp));
}

// Just disabled on sanitizers because it doesn't need to run often. It's just a unit test.
TEST_F(MasterTestEnt, YB_DISABLE_TEST_IN_SANITIZERS(TestDeleteCDCStreamNoForceDelete)) {
  // #12255.  Added 'force_delete' flag, but only run this check if the client code specifies it.
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  CDCStreamId stream_id;
  FLAGS_cdc_state_table_num_tablets = 1;
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

    stream_id = resp.stream_id();
  }

  GetCDCStreamResponsePB resp;
  ASSERT_OK(GetCDCStream(stream_id, &resp));
  ASSERT_EQ(resp.stream().table_id().Get(0), table_id);

  // Should succeed because we don't use the 'force_delete' safety check in this API call.
  ASSERT_OK(DeleteCDCStream(stream_id));

  resp.Clear();
  ASSERT_NOK(GetCDCStream(stream_id, &resp));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

TEST_F(MasterTestEnt, TestListCDCStreams) {
  TableId table_id;
  ASSERT_OK(CreateTable(kTableName, kTableSchema, &table_id));

  CDCStreamId stream_id;
  FLAGS_cdc_state_table_num_tablets = 1;
  ASSERT_OK(CreateCDCStream(table_id, &stream_id));

  ListCDCStreamsResponsePB resp;
  ASSERT_OK(ListCDCStreams(&resp));
  ASSERT_EQ(1, resp.streams_size());
  ASSERT_EQ(stream_id, resp.streams(0).stream_id());
}

TEST_F(MasterTestEnt, TestSetupUniverseReplication) {
  std::string producer_id = "producer_universe";
  std::vector<std::string> producer_masters {"127.0.0.1:7100"};
  std::vector<std::string> tables {"some_table_id"};
  // Always fails because we don't have actual producer.
  ASSERT_NOK(SetupUniverseReplication(producer_id, producer_masters, tables));

  GetUniverseReplicationResponsePB resp;
  ASSERT_OK(GetUniverseReplication(producer_id, &resp));
  ASSERT_EQ(resp.entry().producer_id(), producer_id);

  ASSERT_EQ(resp.entry().producer_master_addresses_size(), 1);
  std::string addr;
  const auto& hp = resp.entry().producer_master_addresses(0);
  addr = hp.host() + ":" + std::to_string(hp.port());
  ASSERT_EQ(addr, "127.0.0.1:7100");

  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), "some_table_id");
}

TEST_F(MasterTestEnt, TestDeleteUniverseReplication) {
  std::string producer_id = "producer_universe";
  std::vector<std::string> producer_masters {"127.0.0.1:7100"};
  std::vector<std::string> tables {"some_table_id"};
  // Always fails because we don't have actual producer.
  ASSERT_NOK(SetupUniverseReplication(producer_id, producer_masters, tables));

  // Verify that universe was created.
  GetUniverseReplicationResponsePB resp;
  ASSERT_OK(GetUniverseReplication(producer_id, &resp));
  ASSERT_EQ(resp.entry().producer_id(), producer_id);

  ASSERT_OK(DeleteUniverseReplication(producer_id));

  resp.Clear();
  ASSERT_NOK(GetUniverseReplication(producer_id, &resp));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(MasterErrorPB::OBJECT_NOT_FOUND, resp.error().code());
}

} // namespace enterprise
} // namespace master
} // namespace yb
