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
#include <gflags/gflags.h>
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

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cdcsdk_test_base.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master.h"
#include "yb/master/master_client.pb.h"
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
class CDCSDKStreamTest : public CDCSDKTestBase {
 public:
  struct ExpectedRecord {
    std::string key;
    std::string value;
  };

  Status DeleteCDCStream(const std::string& db_stream_id) {
    RpcController delete_rpc;
    delete_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    DeleteCDCStreamRequestPB delete_req;
    DeleteCDCStreamResponsePB delete_resp;
    delete_req.add_stream_id(db_stream_id);

    // The following line assumes that cdc_proxy_ has been initialized in the test already
    return cdc_proxy_->DeleteCDCStream(delete_req, &delete_resp, &delete_rpc);
  }

  Result<std::vector<std::string>> CreateDBStreams(const int num_streams) {
    std::vector<std::string> created_streams;
    // We will create some DB Streams to be listed out later.
    for (int i = 0; i < num_streams; i++) {
      std::string db_stream_id = VERIFY_RESULT(CreateDBStream());
      SCHECK(!db_stream_id.empty(), IllegalState, "The created db_stream_id is empty!");
      created_streams.push_back(db_stream_id);
    }

    // Sorting the stream IDs in order to simplify assertion.
    std::sort(created_streams.begin(), created_streams.end());
    return created_streams;
  }

  Result<google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB>> ListDBStreams(
      const std::string& namespace_name = kNamespaceName, const TableId table_id = "") {
    // Listing the streams now.
    master::ListCDCStreamsRequestPB list_req;
    master::ListCDCStreamsResponsePB list_resp;

    // If table_id is passed i.e. it is not empty, it means that now the xCluster streams are being
    // requested, so we will be doing further operations based on the same check.
    if (!table_id.empty()) {
      list_req.set_id_type(master::IdTypePB::TABLE_ID);
      list_req.set_table_id(table_id);
    } else {
      list_req.set_id_type(master::IdTypePB::NAMESPACE_ID);
      list_req.set_namespace_id(VERIFY_RESULT(GetNamespaceId(kNamespaceName)));
    }

    RpcController list_rpc;
    list_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    master::MasterReplicationProxy master_proxy_(
        &test_client()->proxy_cache(),
        VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    RETURN_NOT_OK(master_proxy_.ListCDCStreams(list_req, &list_resp, &list_rpc));

    if (list_resp.has_error()) {
      return StatusFromPB(list_resp.error().status());
    }

    return list_resp.streams();
  }

  Result<master::GetCDCDBStreamInfoResponsePB> GetDBStreamInfo(std::string db_stream_id) {
    master::GetCDCDBStreamInfoRequestPB get_req;
    master::GetCDCDBStreamInfoResponsePB get_resp;
    get_req.set_db_stream_id(db_stream_id);

    RpcController get_rpc;
    get_rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    master::MasterReplicationProxy master_proxy_(
        &test_client()->proxy_cache(),
        VERIFY_RESULT(test_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr()));

    RETURN_NOT_OK(master_proxy_.GetCDCDBStreamInfo(get_req, &get_resp, &get_rpc));

    return get_resp;
  }

  void TestListDBStreams(bool with_table) {
    // Create one table.
    std::string table_id;

    if (with_table) {
      auto table =
          ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

      // Get the table_id of the created table.
      table_id = ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, kTableName));
    }
    // We will create some DB Streams to be listed out later.
    auto created_streams = ASSERT_RESULT(CreateDBStreams(3));

    const size_t total_created_streams = created_streams.size();

    google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_streams =
        ASSERT_RESULT(ListDBStreams());

    const uint32_t num_streams = list_streams.size();
    ASSERT_EQ(total_created_streams, num_streams);

    std::vector<std::string> resp_stream_ids;
    for (uint32_t i = 0; i < num_streams; ++i) {
      if (with_table) {
        // Since there is one table, all the streams would contain one table_id in their response.
        ASSERT_EQ(1, list_streams.Get(i).table_id_size());
        // That particular table_id would be equal to the created table id.
        ASSERT_EQ(table_id, list_streams.Get(i).table_id(0));
      } else {
        // Since there are no tables in DB, there would be no table_ids in the response.
        ASSERT_EQ(0, list_streams.Get(i).table_id_size());
      }
      resp_stream_ids.push_back(list_streams.Get(i).stream_id());
    }
    // Sorting to simplify assertion.
    std::sort(resp_stream_ids.begin(), resp_stream_ids.end());

    // Verify if the stream ids returned with the response are the same as the ones created.
    for (uint32_t i = 0; i < resp_stream_ids.size(); ++i) {
      ASSERT_EQ(created_streams[i], resp_stream_ids[i]);
    }
  }

  void TestDBStreamInfo(
      const vector<std::string>& table_with_pk, const vector<std::string>& table_without_pk) {
    std::vector<std::string>::size_type num_of_tables_with_pk = table_with_pk.size();

    for (const auto& table_name : table_with_pk) {
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, table_name));
    }

    for (const auto& table_name : table_without_pk) {
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, table_name,
                                1 /* num_tablets */, false));
    }

    std::vector<std::string> created_table_ids_with_pk;

    for (const auto& table_name : table_with_pk) {
      created_table_ids_with_pk.push_back(
          ASSERT_RESULT(GetTableId(&test_cluster_, kNamespaceName, table_name)));
    }

    std::vector<std::string> created_table_ids_without_pk;

    // Sorting would make assertion easier later on.
    std::sort(created_table_ids_with_pk.begin(), created_table_ids_with_pk.end());
    std::string db_stream_id = ASSERT_RESULT(CreateDBStream());

    auto get_resp = ASSERT_RESULT(GetDBStreamInfo(db_stream_id));
    ASSERT_FALSE(get_resp.has_error());

    // Get the namespace ID.
    std::string namespace_id = ASSERT_RESULT(GetNamespaceId(kNamespaceName));

    // We have only 1 table, so the response will (should) have 1 table info only.
    uint32_t table_info_size = get_resp.table_info_size();
    ASSERT_EQ(num_of_tables_with_pk, table_info_size);

    // Check whether the namespace ID in the response is correct.
    ASSERT_EQ(namespace_id, get_resp.namespace_id());

    // Store the table IDs received in the response.
    std::vector<std::string> table_ids_in_resp;
    for (uint32_t i = 0; i < table_info_size; ++i) {
      // Also assert that all the table_info(s) contain the same db_stream_id.
      ASSERT_EQ(db_stream_id, get_resp.table_info(i).stream_id());

      table_ids_in_resp.push_back(get_resp.table_info(i).table_id());
    }
    std::sort(table_ids_in_resp.begin(), table_ids_in_resp.end());

    // Verifying that the table IDs received in the response are for the tables which were
    // created earlier.
    for (uint32_t i = 0; i < table_ids_in_resp.size(); ++i) {
      ASSERT_EQ(created_table_ids_with_pk[i], table_ids_in_resp[i]);
    }
  }
};

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateCDCSDKStreamImplicit)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  std::string db_stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateCDCSDKStreamExplicit)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // The function CreateDBStream() creates a stream with EXPLICIT checkpointing by default.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

// This test is to verify the fix for the following:
// [#10945] Error while creating a DB Stream if any table in the database is without a primary key.
TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(TestStreamCreation)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // Create a table with primary key.
  auto table1 =
      ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, "table_with_pk"));
  // Create another table without primary key.
  auto table2 = ASSERT_RESULT(
      CreateTable(&test_cluster_, kNamespaceName, "table_without_pk", 1 /* num_tablets */, false));

  // We have a table with primary key and one without primary key so while creating
  // the DB Stream ID, the latter one will be ignored and will not be a part of streaming with CDC.
  // Now we just need to ensure that everything is working fine.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(TestOnSingleRF)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(1, 1, false));

  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DeleteDBStream)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // Create a DB Stream ID to be deleted later on.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream());
  ASSERT_NE(0, db_stream_id.length());

  // Deleting the created DB Stream ID.
  ASSERT_OK(DeleteCDCStream(db_stream_id));
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CreateMultipleStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto stream_ids = ASSERT_RESULT(CreateDBStreams(3));
  ASSERT_EQ(3, stream_ids.size());
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DeleteMultipleStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  auto stream_ids = ASSERT_RESULT(CreateDBStreams(3));
  ASSERT_EQ(3, stream_ids.size());

  for (const auto& stream_id : stream_ids) {
    // Since we have created 3 streams, we will be deleting 3 streams too.
    ASSERT_OK(DeleteCDCStream(stream_id));
  }
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ListDBStreams)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestListDBStreams(true);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ListDBStreams_NoTablesInDB)) {
  // Setup cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestListDBStreams(false);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestDBStreamInfo(std::vector<std::string>{kTableName}, {});
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_MultipleTablesInDB)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  std::vector<std::string> table_names_with_pk = {
      "pk_table1", "pk_table2", "pk_table3", "pk_table4"};
  std::vector<std::string> table_names_without_pk = {"table_without_pk"};

  TestDBStreamInfo(table_names_with_pk, table_names_without_pk);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_NoTablesInDB)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  TestDBStreamInfo({}, {});
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(DBStreamInfoTest_AllTablesWithoutPrimaryKey)) {
  // Set up a cluster with RF 3.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  std::vector<std::string> table_names_without_pk = {"table_without_pk_1", "table_without_pk_2"};

  TestDBStreamInfo({}, table_names_without_pk);
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(CDCWithXclusterEnabled)) {
  // Set up an RF 3 cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));
  auto table = ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, kTableName));

  // We not need to create both xcluster and cdc streams on a table,
  // and we will list them to check that they are not the same.

  const uint32_t num_of_streams = 100;

  // Creating CDC DB streams on the table.
  // We get a sorted vector from CreateDBStreams() function already.
  std::vector<CDCStreamId> created_db_streams = ASSERT_RESULT(CreateDBStreams(num_of_streams));

  // Creating xCluster streams now.
  std::vector<CDCStreamId> created_xcluster_streams;
  for (uint32_t i = 0; i < num_of_streams; ++i) {
    RpcController rpc;
    CreateCDCStreamRequestPB create_req;
    CreateCDCStreamResponsePB create_resp;

    create_req.set_table_id(table.table_id());
    ASSERT_OK(cdc_proxy_->CreateCDCStream(create_req, &create_resp, &rpc));

    // Assert that there is no DB stream ID in the response while creating xCluster stream.
    ASSERT_FALSE(create_resp.has_db_stream_id());

    created_xcluster_streams.push_back(create_resp.stream_id());
  }
  std::sort(created_xcluster_streams.begin(), created_xcluster_streams.end());

  // Ensure that created streams are all different.
  for (uint32_t i = 0; i < num_of_streams; ++i) {
    ASSERT_NE(created_db_streams[i], created_xcluster_streams[i]);
  }

  // List streams for CDC and xCluster. They both should not be the same.
  google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_cdc_resp =
      ASSERT_RESULT(ListDBStreams(kNamespaceName));
  std::vector<std::string> db_streams;
  for (int32_t i = 0; i < list_cdc_resp.size(); ++i) {
    db_streams.push_back(list_cdc_resp.Get(i).stream_id());
  }
  std::sort(db_streams.begin(), db_streams.end());

  // List the streams for xCluster.
  google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_xcluster_resp =
      ASSERT_RESULT(ListDBStreams(kNamespaceName, table.table_id()));
  std::vector<std::string> xcluster_streams;
  for (int32_t i = 0; i < list_xcluster_resp.size(); ++i) {
    xcluster_streams.push_back(list_xcluster_resp.Get(i).stream_id());
  }
  std::sort(xcluster_streams.begin(), xcluster_streams.end());

  // Ensuring that the streams we got in both the cases are different in order to make sure that
  // there are no clashes.
  for (uint32_t i = 0; i < num_of_streams; ++i) {
    ASSERT_NE(db_streams[i], xcluster_streams[i]);
  }
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ImplicitCheckPointValidate)) {
  // Create a cluster.
  ASSERT_OK(SetUpWithParams(3, 1, false));

  // Create a DB Stream.
  std::string db_stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::IMPLICIT));
  ASSERT_NE(0, db_stream_id.length());

  // Get the list of dbstream.
  google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_streams =
      ASSERT_RESULT(ListDBStreams(kNamespaceName));
  const uint32_t num_streams = list_streams.size();

  for (uint32_t i = 0; i < num_streams; ++i) {
    // Validate the streamid.
    ASSERT_EQ(db_stream_id, list_streams.Get(i).stream_id());

    const uint32_t options_sz = list_streams.Get(i).options_size();
    for (uint32_t j = 0; j < options_sz; j++) {
      // Validate the checkpoint type IMPLICIT.
      string cur_key = list_streams.Get(i).options(j).key();
      string cur_value = list_streams.Get(i).options(j).value();
      if (cur_key == string("checkpoint_type")) {
        ASSERT_EQ(cur_value, string("IMPLICIT"));
      }
    }
  }
}

TEST_F(CDCSDKStreamTest, YB_DISABLE_TEST_IN_TSAN(ExplicitCheckPointValidate)) {
    // Create a cluster.
    ASSERT_OK(SetUpWithParams(3, 1, false));

    // Create a DB Stream.
    std::string db_stream_id = ASSERT_RESULT(CreateDBStream(CDCCheckpointType::EXPLICIT));
    ASSERT_NE(0, db_stream_id.length());

    // Get the list of dbstream.
    google::protobuf::RepeatedPtrField<yb::master::CDCStreamInfoPB> list_streams =
        ASSERT_RESULT(ListDBStreams(kNamespaceName));
    const uint32_t num_streams = list_streams.size();

    for (uint32_t i = 0; i < num_streams; ++i) {
      // Validate the streamid.
      ASSERT_EQ(db_stream_id, list_streams.Get(i).stream_id());

      const uint32_t options_sz = list_streams.Get(i).options_size();
      for (uint32_t j = 0; j < options_sz; j++) {
        // Validate the checkpoint type EXPLICIT.
        string cur_key = list_streams.Get(i).options(j).key();
        string cur_value = list_streams.Get(i).options(j).value();
        if (cur_key == string("checkpoint_type")) {
          ASSERT_EQ(cur_value, string("EXPLICIT"));
        }
      }
    }
}

TEST_F(CDCSDKStreamTest, DropNamespaceWithLiveCDCStream) {
  ASSERT_OK(
      SetUpWithParams(3 /* replication_factor */, 1 /* num_masters */, false /* colocated */));
  std::vector<std::string> table_names = {"pk_table1", "pk_table2"};

  for (const auto& table_name : table_names) {
    ASSERT_RESULT(CreateTable(&test_cluster_, kNamespaceName, table_name));
  }

  ASSERT_RESULT(CreateDBStream());
  ASSERT_OK(DropDatabase(&test_cluster_));
}

}  // namespace enterprise
}  // namespace cdc
}  // namespace yb
