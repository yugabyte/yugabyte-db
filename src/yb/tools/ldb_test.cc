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

#include <string>
#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/common/ql_value.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/rpc/messenger.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tools/data_gen_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/util/date_time.h"
#include "yb/util/path_util.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"


using namespace std::literals;

namespace yb {
namespace tools {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBTableCreator;
using client::YBTableName;
using client::YBTable;

static const char* const kTabletUtilToolName = "ldb";
static const char* const kNamespace = "ldb_test_namespace";
static const char* const kTableName = "my_table";
static constexpr int32_t kNumTablets = 1;
static constexpr int32_t kNumTabletServers = 1;

class YBTabletUtilTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  YBTabletUtilTest() : random_(0) {
  }

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;

    opts.num_tablet_servers = kNumTabletServers;

    cluster_.reset(new MiniCluster(env_.get(), opts));
    ASSERT_OK(cluster_->Start());

    client_ = ASSERT_RESULT(YBClientBuilder()
        .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
        .Build());

    YBSchemaBuilder b;
    b.AddColumn("k")->Type(INT64)->NotNull()->HashPrimaryKey();
    ASSERT_OK(b.Build(&schema_));

    client_ = ASSERT_RESULT(cluster_->CreateClient());
    client_messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("Client").Build());
    rpc::ProxyCache proxy_cache(client_messenger_.get());
    proxy_.reset(new master::MasterServiceProxy(&proxy_cache,
                                                cluster_->leader_mini_master()->bound_rpc_addr()));

    // Create the namespace.
    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    table_name_.reset(new YBTableName(YQL_DATABASE_CQL, kNamespace, kTableName));
    std::unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(*table_name_.get())
        .table_type(client::YBTableType::YQL_TABLE_TYPE)
        .schema(&schema_)
        .num_tablets(kNumTablets)
        .wait(true)
        .Create());

    ASSERT_OK(client_->OpenTable(*table_name_, &table_));
  }

  void DoTearDown() override {
    client_messenger_->Shutdown();
    client_.reset();
    cluster_->Shutdown();
  }

 protected:

  CHECKED_STATUS WriteData() {
    auto session = client_->NewSession();
    session->SetTimeout(5s);

    client::TableHandle table;
    RETURN_NOT_OK(table.Open(*table_name_, client_.get()));
    auto insert = table.NewInsertOp();
    auto req = insert->mutable_request();
    GenerateDataForRow(schema_, 17 /* record_id */, &random_, req);

    RETURN_NOT_OK(session->Apply(insert));
    RETURN_NOT_OK(session->Flush());
    return Status::OK();
  }

  string FormatDbPath(const string& root, const string& table_id, const string& tablet_id) {
    return strings::Substitute(
        "$0/yb-data/tserver/data/rocksdb/table-$1/tablet-$2",
        root, table_id, tablet_id);
  }

  Result<string> GetTabletDbPath() {
    auto tablet_data_root = cluster_->GetTabletServerFsRoot(0);

    master::GetTableLocationsRequestPB get_tablets_req;
    master::GetTableLocationsResponsePB get_tablets_resp;
    rpc::RpcController controller;

    get_tablets_req.mutable_table()->set_table_name(table_name_->table_name());
    get_tablets_req.mutable_table()->mutable_namespace_()->set_name(kNamespace);
    get_tablets_req.set_max_returned_locations(kNumTablets);
    RETURN_NOT_OK(proxy_->GetTableLocations(get_tablets_req, &get_tablets_resp, &controller));
    if (get_tablets_resp.has_error()) {
      return STATUS(InternalError, get_tablets_resp.ShortDebugString());
    }
    if (get_tablets_resp.tablet_locations_size() != kNumTablets) {
      return STATUS_FORMAT(
          InternalError,
          "Unexpected number of tablets: $0.", get_tablets_resp.tablet_locations_size());
    }

    auto tablet_id = get_tablets_resp.tablet_locations(0).tablet_id();
    return FormatDbPath(tablet_data_root, table_->id(), tablet_id);
  }

  std::unique_ptr<YBClient> client_;
  YBSchema schema_;
  std::unique_ptr<YBTableName> table_name_;
  std::shared_ptr<YBTable> table_;
  std::unique_ptr<master::MasterServiceProxy> proxy_;
  std::unique_ptr<rpc::Messenger> client_messenger_;
  Random random_;
};


TEST_F(YBTabletUtilTest, VerifySingleKeyIsFound) {
  string output;
  ASSERT_OK(WriteData());
  ASSERT_OK(client_->FlushTable(*table_name_, 5 /* timeout_secs */, false /* is_compaction */));
  string db_path = ASSERT_RESULT(GetTabletDbPath());

  vector<string> argv = {
    GetToolPath(kTabletUtilToolName),
    "dump",
    "--compression_type=snappy",
    "--db=" + db_path
  };
  ASSERT_OK(Subprocess::Call(argv, &output, false /* read_stderr */));

  ASSERT_NE(output.find("Keys in range: 1"), string::npos);
}

} // namespace tools
} // namespace yb
