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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tools/data_gen_util.h"

#include "yb/util/path_util.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;

using namespace std::literals;

namespace yb {
namespace tools {

using client::YBClient;
using client::YBSchema;
using client::YBSchemaBuilder;
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

    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    YBSchema schema;
    YBSchemaBuilder b;
    b.AddColumn("k")->Type(DataType::INT64)->NotNull()->HashPrimaryKey();
    ASSERT_OK(b.Build(&schema));

    client_ = ASSERT_RESULT(cluster_->CreateClient());

    // Create the namespace.
    ASSERT_OK(client_->CreateNamespace(kNamespace));

    // Create the table.
    const YBTableName table_name(YQL_DATABASE_CQL, kNamespace, kTableName);
    ASSERT_OK(client_
        ->NewTableCreator()
        ->table_name(table_name)
        .table_type(client::YBTableType::YQL_TABLE_TYPE)
        .schema(&schema)
        .num_tablets(kNumTablets)
        .wait(true)
        .Create());

    ASSERT_OK(client_->OpenTable(table_name, &table_));
  }

  void DoTearDown() override {
    client_.reset();
    cluster_->Shutdown();
  }

 protected:

  Status WriteData() {
    auto session = client_->NewSession(5s);

    std::shared_ptr<client::YBqlWriteOp> insert(table_->NewQLWrite());
    auto req = insert->mutable_request();
    GenerateDataForRow(table_->schema(), 17 /* record_id */, &random_, req);

    session->Apply(insert);
    RETURN_NOT_OK(session->TEST_Flush());
    return Status::OK();
  }

  Result<string> GetTabletDbPath() {
    for (const auto& peer : cluster_->GetTabletPeers(0)) {
      if (peer->tablet_metadata()->table_name() == kTableName) {
        return peer->tablet_metadata()->rocksdb_dir();
      }
    }
    return STATUS(IllegalState, "Did not find tablet peer with YCQL table");
  }

  std::unique_ptr<YBClient> client_;
  std::shared_ptr<YBTable> table_;
  Random random_;
};


TEST_F(YBTabletUtilTest, VerifySingleKeyIsFound) {
  string output;
  ASSERT_OK(WriteData());
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  string db_path = ASSERT_RESULT(GetTabletDbPath());

  vector<string> argv = {
    GetToolPath(kTabletUtilToolName),
    "dump",
    "--compression_type=snappy",
    "--db=" + db_path
  };
  ASSERT_OK(Subprocess::Call(argv, &output));

  ASSERT_NE(output.find("Keys in range: 1"), string::npos);
}

TEST_F(YBTabletUtilTest, DumpManifestFile) {
    string output;
  ASSERT_OK(WriteData());
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  string db_path = ASSERT_RESULT(GetTabletDbPath());

  vector<string> argv = {
    GetToolPath(kTabletUtilToolName),
    "manifest_dump",
    "--db=" + db_path
  };

  // Make sure LDB is not crashing
  ASSERT_OK(Subprocess::Call(argv, &output));
}

} // namespace tools
} // namespace yb
