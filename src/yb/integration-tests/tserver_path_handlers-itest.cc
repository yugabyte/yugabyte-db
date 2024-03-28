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

#include <string>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/curl_util.h"
#include "yb/util/jsonreader.h"

namespace yb {

using std::string;

const uint kNumMasters(3);
const uint kNumTservers(3);

class TServerPathHandlersItest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void InitCluster() {
    MiniClusterOptions opts;
    opts.num_tablet_servers = kNumTservers;
    opts.num_masters = kNumMasters;
    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
  }

  void SetUp() override {
    YBMiniClusterTestBase<MiniCluster>::SetUp();
    InitCluster();

    auto tserver_http_endpoint = cluster_->mini_tablet_server(0)->bound_http_addr();
    tserver_http_url_ = "http://" + AsString(tserver_http_endpoint);
  }

  void DoTearDown() override {
    LOG(INFO) << "Calling DoTearDown";
    cluster_->Shutdown();
  }

 protected:
  Result<string> FetchURL(const string& query_path) {
    faststring result;
    RETURN_NOT_OK(EasyCurl().FetchURL(tserver_http_url_ + query_path, &result));
    return result.ToString();
  }

  string tserver_http_url_;
};

TEST_F(TServerPathHandlersItest, TestMasterPathHandlers) {
  faststring result;
  // TServer HTML paths.
  ASSERT_OK(FetchURL("/"));
  ASSERT_OK(FetchURL("/tables"));
  ASSERT_OK(FetchURL("/tablets"));
  ASSERT_OK(FetchURL("/operations"));
  ASSERT_OK(FetchURL("/tablet-consensus-status"));
  ASSERT_OK(FetchURL("/log-anchors"));
  ASSERT_OK(FetchURL("/transactions"));
  ASSERT_OK(FetchURL("/rocksdb"));
  ASSERT_OK(FetchURL("/waitqueue"));
  ASSERT_OK(FetchURL("/api/v1/meta-cache"));
#ifndef NDEBUG
  ASSERT_OK(FetchURL("/intentsdb"));
#endif
  ASSERT_OK(FetchURL("/maintenance-manager"));

  // Default paths.
  ASSERT_OK(FetchURL("/logs"));
  ASSERT_OK(FetchURL("/varz"));
  ASSERT_OK(FetchURL("/status"));
  ASSERT_OK(FetchURL("/memz"));
  ASSERT_OK(FetchURL("/mem-trackers"));
  ASSERT_OK(FetchURL("/api/v1/mem-trackers"));
  ASSERT_OK(FetchURL("/api/v1/varz"));
  ASSERT_OK(FetchURL("/api/v1/version-info"));

  // API paths.
  ASSERT_OK(FetchURL("/api/v1/health-check"));
  ASSERT_OK(FetchURL("/api/v1/version"));
  ASSERT_OK(FetchURL("/api/v1/masters"));
  ASSERT_OK(FetchURL("/api/v1/tablets"));
}

TEST_F(TServerPathHandlersItest, TestVarzAutoFlag) {
  static const auto kExpectedAutoFlag = "ysql_yb_enable_expression_pushdown";

  // In Non LTO builds the unexpected AutoFlag will not be found. In LTO builds and MiniCluster
  // tests the flag will appear in the Default section instead of the AutoFlags section.
  static const auto kUnExpectedAutoFlag = "use_parent_table_id_field";

  // Test the HTML endpoint.
  static const auto kAutoFlagsStart = "<h2>Auto Flags</h2>";
  static const auto kAutoFlagsEnd = "<h2>Default Flags</h2>";

  auto result = ASSERT_RESULT(FetchURL("/varz"));

  auto it_auto_flags_start = result.find(kAutoFlagsStart);
  ASSERT_NE(it_auto_flags_start, std::string::npos);
  auto it_auto_flags_end = result.find(kAutoFlagsEnd);
  ASSERT_NE(it_auto_flags_end, std::string::npos);

  auto it_expected_flag = result.find(kExpectedAutoFlag);
  ASSERT_GT(it_expected_flag, it_auto_flags_start);
  ASSERT_LT(it_expected_flag, it_auto_flags_end);

  auto it_unexpected_flag = result.find(kUnExpectedAutoFlag);
  ASSERT_GT(it_unexpected_flag, it_auto_flags_end);

  // Test the JSON API endpoint.
  result = ASSERT_RESULT(FetchURL("/api/v1/varz"));

  JsonReader r(result);
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_obj = nullptr;
  ASSERT_OK(r.ExtractObject(r.root(), NULL, &json_obj));
  ASSERT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_obj)->GetType());
  ASSERT_TRUE(json_obj->HasMember("flags"));
  ASSERT_EQ(rapidjson::kArrayType, (*json_obj)["flags"].GetType());
  const rapidjson::Value::ConstArray flags = (*json_obj)["flags"].GetArray();

  auto it_expected_json_flag = std::find_if(flags.Begin(), flags.End(), [](const auto& flag) {
    return flag["name"] == kExpectedAutoFlag;
  });
  ASSERT_NE(it_expected_json_flag, flags.End());
  ASSERT_EQ((*it_expected_json_flag)["type"], "Auto");

  auto it_unexpected_json_flag = std::find_if(flags.Begin(), flags.End(), [](const auto& flag) {
    return flag["name"] == kUnExpectedAutoFlag;
  });

  ASSERT_NE(it_unexpected_json_flag, flags.End());
  ASSERT_EQ((*it_unexpected_json_flag)["type"], "Default");
}

void VerifyMetaCacheObjectIsValid(
    const rapidjson::Value* json_object, const JsonReader& json_reader) {
  EXPECT_TRUE(json_object->HasMember("MainMetaCache"));

  const rapidjson::Value* main_metacache = nullptr;
  EXPECT_OK(json_reader.ExtractObject(json_object, "MainMetaCache", &main_metacache));
  EXPECT_TRUE(main_metacache->HasMember("tablets"));

  std::vector<const rapidjson::Value*> remote_tablets;
  ASSERT_OK(json_reader.ExtractObjectArray(main_metacache, "tablets", &remote_tablets));
  for (auto remote_tablet : remote_tablets) {
    EXPECT_TRUE(remote_tablet->HasMember("tablet_id"));
    EXPECT_TRUE(remote_tablet->HasMember("replicas"));
  }
}

TEST_F(TServerPathHandlersItest, TestListMetaCache) {
  auto result = ASSERT_RESULT(FetchURL("/api/v1/meta-cache"));
  JsonReader r(result);
  ASSERT_OK(r.Init());
  const rapidjson::Value* json_object = nullptr;
  EXPECT_OK(r.ExtractObject(r.root(), NULL, &json_object));
  EXPECT_EQ(rapidjson::kObjectType, CHECK_NOTNULL(json_object)->GetType());
  VerifyMetaCacheObjectIsValid(json_object, r);
}

}  // namespace yb
