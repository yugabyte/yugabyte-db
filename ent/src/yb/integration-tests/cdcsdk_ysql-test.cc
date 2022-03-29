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

#include <algorithm>
#include <chrono>
#include <utility>
#include <boost/assign.hpp>
#include <gtest/gtest.h>

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBError;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTableType;
using master::GetNamespaceInfoResponsePB;
using master::MiniMaster;
using tserver::MiniTabletServer;
using tserver::enterprise::CDCConsumer;

using pgwrapper::GetInt32;
using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;
using pgwrapper::PgSupervisor;
using pgwrapper::ToString;

using rpc::RpcController;

namespace cdc {
namespace enterprise {

class CDCSDKYsqlTest : public CDCSDKTestBase {
 public:
  struct ExpectedRecord {
    int32_t key;
    int32_t value;
  };

  Result<string> GetUniverseId(Cluster* cluster) {
    yb::master::GetMasterClusterConfigRequestPB req;
    yb::master::GetMasterClusterConfigResponsePB resp;

    master::MasterClusterProxy master_proxy(
        &cluster->client_->proxy_cache(),
        VERIFY_RESULT(cluster->mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy.GetMasterClusterConfig(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Error getting cluster config");
    }
    return resp.cluster_config().cluster_uuid();
  }

  Status DropDB(Cluster* cluster) {
    const std::string db_name = "testdatabase";
    RETURN_NOT_OK(CreateDatabase(&test_cluster_, db_name, true));
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(db_name));
    RETURN_NOT_OK(conn.ExecuteFormat("DROP DATABASE $0", kNamespaceName));
    return Status::OK();
  }

  // The range is exclusive of end i.e. [start, end)
  void WriteRows(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s)";

    for (uint32_t i = start; i < end; ++i) {
      EXPECT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
  }

  void WriteRowsInTransaction(uint32_t start, uint32_t end, Cluster* cluster) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(kNamespaceName));
    LOG(INFO) << "Writing " << end - start << " row(s) within transaction";

    EXPECT_OK(conn.Execute("BEGIN"));
    for (uint32_t i = start; i < end; ++i) {
      EXPECT_OK(conn.ExecuteFormat(
          "INSERT INTO $0($1, $2) VALUES ($3, $4)", kTableName, kKeyColumnName, kValueColumnName, i,
          i + 1));
    }
    EXPECT_OK(conn.Execute("COMMIT"));
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(0).tablet_id());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
  }

  void PrepareChangeRequest(
      GetChangesRequestPB* change_req, const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      const CDCSDKCheckpointPB& cp) {
    change_req->set_stream_id(stream_id);
    change_req->set_tablet_id(tablets.Get(0).tablet_id());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
    change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
  }

  void PrepareSetCheckpointRequest(
      SetCDCCheckpointRequestPB* set_checkpoint_req,
      const CDCStreamId stream_id,
      google::protobuf::RepeatedPtrField<master::TabletLocationsPB>
          tablets) {
    set_checkpoint_req->set_stream_id(stream_id);
    set_checkpoint_req->set_tablet_id(tablets.Get(0).tablet_id());
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(0);
    set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(0);
  }

  CHECKED_STATUS SetInitialCheckpoint(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    RpcController set_checkpoint_rpc;
    SetCDCCheckpointRequestPB set_checkpoint_req;
    SetCDCCheckpointResponsePB set_checkpoint_resp;
    auto deadline = CoarseMonoClock::now() + test_client()->default_rpc_timeout();
    set_checkpoint_rpc.set_deadline(deadline);
    PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);

    return cdc_proxy_->SetCDCCheckpoint(set_checkpoint_req,
                                        &set_checkpoint_resp,
                                        &set_checkpoint_rpc);
  }

  void AssertKeyValue(const CDCSDKProtoRecordPB& record, const int32_t& key, const int32_t& value) {
    ASSERT_EQ(key, record.row_message().new_tuple(0).datum_int32());
    ASSERT_EQ(value, record.row_message().new_tuple(1).datum_int32());
  }

  Result<GetChangesResponsePB> GetChangesFromCDC(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
    GetChangesRequestPB change_req;
    GetChangesResponsePB change_resp;
    PrepareChangeRequest(&change_req, stream_id, tablets);

    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(change_req, &change_resp, &get_changes_rpc));

    if (change_resp.has_error()) {
      return StatusFromPB(change_resp.error().status());
    }

    return change_resp;
  }

  void TestGetChanges(const uint32_t replication_factor,
                      bool add_tables_without_primary_key = false) {
    ASSERT_OK(SetUpWithParams(replication_factor, 1, false));

    auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

    if (add_tables_without_primary_key) {
      // Adding tables without primary keys, they should not disturb any CDC related processes.
      std::string tables_wo_pk[] = {"table_wo_pk_1", "table_wo_pk_2", "table_wo_pk_3"};
      for (const auto& table_name : tables_wo_pk) {
        auto temp = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName,
                                              table_name, 1 /* num_tablets */, false));
      }
    }

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(test_client()->GetTablets(
        table, 0, &tablets,
        /* partition_list_version =*/nullptr));
    ASSERT_EQ(tablets.size(), 1);

    std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream());

    ASSERT_OK(SetInitialCheckpoint(stream_id, tablets));

    WriteRows(0 /* start */, 1 /* end */, &test_cluster_);

    const uint32_t expected_records_size = 1;
    int expected_record[] = {0 /* key */, 1 /* value */};

    sleep(5);
    GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

    uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
    uint32_t ins_count = 0;
    for (uint32_t i = 0; i < record_size; ++i) {
      if (change_resp.cdc_sdk_proto_records(i).row_message().op() == RowMessage::INSERT) {
        const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
        AssertKeyValue(record, expected_record[0], expected_record[1]);
        ++ins_count;
      }
    }
    LOG(INFO) << "Got " << ins_count << " insert records";
    ASSERT_EQ(expected_records_size, ins_count);
  }

  Result<GetChangesResponsePB> VerifyIfDDLRecordPresent(
      const CDCStreamId& stream_id,
      const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
      bool expect_ddl_record, bool is_first_call, const CDCSDKCheckpointPB* cp = nullptr) {
    GetChangesRequestPB req;
    GetChangesResponsePB resp;

    if (cp == nullptr) {
      PrepareChangeRequest(&req, stream_id, tablets);
    } else {
      PrepareChangeRequest(&req, stream_id, tablets, *cp);
    }

    // The default value for need_schema_info is false.
    if (expect_ddl_record) {
      req.set_need_schema_info(true);
    }

    RpcController get_changes_rpc;
    RETURN_NOT_OK(cdc_proxy_->GetChanges(req, &resp, &get_changes_rpc));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    auto record = resp.cdc_sdk_proto_records(0);

    // If it's the first call to GetChanges, we will get a DDL record irrespective of the
    // value of need_schema_info.
    if (is_first_call || expect_ddl_record) {
      EXPECT_EQ(record.row_message().op(), RowMessage::DDL);
    } else {
      EXPECT_NE(record.row_message().op(), RowMessage::DDL);
    }

    return resp;
  }
};

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestBaseFunctions)) {
  // setting up a cluster with 3 RF
  ASSERT_OK(SetUpWithParams(3, 1, false /* colocated */));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  ASSERT_FALSE(table.is_cql_namespace());
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestLoadInsertionOnly)) {
  // set up an RF3 cluster
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  WriteRows(0, 10, &test_cluster_);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(GetChangesWithRF1)) {
  TestGetChanges(1 /* replication factor */);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(GetChangesWithRF3)) {
  TestGetChanges(3 /* replication factor */);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(GetChanges_TablesWithNoPKPresentInDB)) {
  TestGetChanges(3 /* replication_factor */, true /* add_tables_without_primary_key */);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(MultiRowInsertion)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets,
      /* partition_list_version = */ nullptr));

  // 1 is the default tablet size we are using while creating the table.
  ASSERT_EQ(tablets.size(), 1);

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream());

  ASSERT_OK(SetInitialCheckpoint(stream_id, tablets));

  WriteRows(0 /* start */, 10 /* end */, &test_cluster_);

  // Records will follow this structure: {key, value}.
  const uint32_t expected_records_size = 10;
  ExpectedRecord expected_records[] = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5},
                                       {5, 6}, {6, 7}, {7, 8}, {8, 9}, {9, 10}};

  sleep(5);

  GetChangesResponsePB change_resp = ASSERT_RESULT(GetChangesFromCDC(stream_id, tablets));

  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  uint32_t ins_count = 0;
  uint32_t idx = 0;
  for (uint32_t i = 0; i < record_size; ++i) {
    if (change_resp.cdc_sdk_proto_records(i).row_message().op() == RowMessage::INSERT) {
      const CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
      AssertKeyValue(record, expected_records[idx].key, expected_records[idx].value);
      ++idx;
      ++ins_count;
    }
  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_records_size, ins_count);
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(DropDatabase)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));
  CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_OK(DropDB(&test_cluster_));
}

TEST_F(CDCSDKYsqlTest, YB_DISABLE_TEST_IN_TSAN(TestNeedSchemaInfoFlag)) {
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(
      table, 0, &tablets, /* partition_list_version = */ nullptr));

  std::string table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
  CDCStreamId stream_id = ASSERT_RESULT(CreateDBStream());

  ASSERT_OK(SetInitialCheckpoint(stream_id, tablets));

  // This will write one row with PK = 0.
  WriteRows(0 /* start */, 1 /* end */, &test_cluster_);

  // This is the first call to GetChanges, we will get a DDL record.
  GetChangesResponsePB resp = ASSERT_RESULT(VerifyIfDDLRecordPresent(stream_id, tablets, false,
                                                                     true));

  // Write another row to the database with PK = 1.
  WriteRows(1 /* start */, 2 /* end */, &test_cluster_);

  // We will not get any DDL record here since this is not the first call and the flag
  // need_schema_info is also unset.
  resp = ASSERT_RESULT(VerifyIfDDLRecordPresent(stream_id, tablets, false, false,
                                                &resp.cdc_sdk_checkpoint()));

  // Write another row to the database with PK = 2.
  WriteRows(2 /* start */, 3 /* end */, &test_cluster_);

  // We will get a DDL record since we have enabled the need_schema_info flag.
  resp = ASSERT_RESULT(VerifyIfDDLRecordPresent(stream_id, tablets, true, false,
                                                &resp.cdc_sdk_checkpoint()));
}
}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
