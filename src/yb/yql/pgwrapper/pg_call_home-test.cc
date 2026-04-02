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

#include <set>

#include <gtest/gtest.h>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_call_home.h"
#include "yb/tserver/ysql_call_home_stats.h"

#include "yb/util/json_document.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::pgwrapper {

using tserver::TserverCallHome;

namespace {

// Subclass to expose the protected BuildJson() for testing.
class TestableTserverCallHome : public TserverCallHome {
 public:
  using TserverCallHome::TserverCallHome;
  using TserverCallHome::BuildJson;
};

class PgCallHomeTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override { return 1; }

  tserver::TabletServer* tablet_server() {
    return cluster_->mini_tablet_server(0)->server();
  }

  std::string BuildCallHomeJson() {
    TestableTserverCallHome call_home(tablet_server());
    auto json = call_home.BuildJson();
    call_home.Shutdown();
    return json;
  }

  JsonValue BuildAndParseJson() {
    json_str_ = BuildCallHomeJson();
    LOG(INFO) << "Call home JSON: " << json_str_;
    auto root = CHECK_RESULT(doc_holder_.Parse(json_str_));
    return root;
  }

  Result<JsonValue> CollectAndParseClusterStats() {
    cluster_stats_json_ = VERIFY_RESULT(tserver::CollectYsqlClusterStatsJson(tablet_server()));
    LOG(INFO) << "Cluster stats JSON: " << cluster_stats_json_;
    return cluster_stats_doc_.Parse(cluster_stats_json_);
  }

 private:
  JsonDocument doc_holder_;
  std::string json_str_;
  JsonDocument cluster_stats_doc_;
  std::string cluster_stats_json_;
};

} // namespace

TEST_F(PgCallHomeTest, EmptyCluster) {
  auto root = BuildAndParseJson();
  ASSERT_TRUE(root["ysql_node_stats"].IsValid());
  ASSERT_TRUE(root["ysql_node_stats"]["aggregate"]["connections"].IsValid());

  auto cluster_root = ASSERT_RESULT(CollectAndParseClusterStats());
  ASSERT_TRUE(cluster_root["aggregate"]["databases"].IsValid());
  auto cluster_dbs = ASSERT_RESULT(cluster_root["databases"].GetArray());
  ASSERT_GT(cluster_dbs.size(), 0);
  for (const auto& cluster_db : cluster_dbs) {
    ASSERT_TRUE(cluster_db["name"].IsValid());
    ASSERT_TRUE(cluster_db["stats"]["extensions"].IsValid());
  }
}

TEST_F(PgCallHomeTest, WeirdDbNames) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(R"(CREATE DATABASE "db_with_""quotes""")"));
  ASSERT_OK(conn.Execute(R"(CREATE DATABASE "db_with_back\slash")"));
  ASSERT_OK(conn.Execute("CREATE DATABASE \"db_with_\xc3\xa9mojis_and_\xc3\xb1\""));

  auto cluster_root = ASSERT_RESULT(CollectAndParseClusterStats());
  auto cluster_dbs = ASSERT_RESULT(cluster_root["databases"].GetArray());

  std::set<std::string> found_names;
  for (const auto& cluster_db : cluster_dbs) {
    auto name = ASSERT_RESULT(cluster_db["name"].GetString());
    found_names.insert(name);
    ASSERT_TRUE(cluster_db["stats"]["extensions"].IsValid())
        << "Missing extensions for database: " << name;
  }

  ASSERT_TRUE(found_names.count(R"(db_with_"quotes")"))
      << "Database with quotes should be in the output";
  ASSERT_TRUE(found_names.count(R"(db_with_back\slash)"))
      << "Database with backslash should be in the output";
  ASSERT_TRUE(found_names.count("db_with_\xc3\xa9mojis_and_\xc3\xb1"))
      << "Database with unicode should be in the output";

  auto root = BuildAndParseJson();
  ASSERT_TRUE(root["ysql_node_stats"].IsValid());
}

} // namespace yb::pgwrapper
