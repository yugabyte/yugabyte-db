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

#pragma once

#include "yb/server/call_home.h"
#include "yb/util/flags.h"
#include "yb/util/jsonreader.h"
#include "yb/util/test_util.h"
#include "yb/common/wire_protocol.h"
#include "yb/util/tsan_util.h"
#include "yb/util/user.h"
#include <boost/asio/ip/tcp.hpp>

DECLARE_string(callhome_collection_level);
DECLARE_string(callhome_tag);
DECLARE_string(callhome_url);
DECLARE_bool(callhome_enabled);
DECLARE_int32(callhome_interval_secs);

DECLARE_string(ysql_pg_conf_csv);
DECLARE_string(ysql_hba_conf_csv);
DECLARE_bool(remote_bootstrap_from_leader_only);

DEFINE_test_flag(bool, call_home_dummy1, false, "Dummy test flag to test call home");
DEFINE_test_flag(bool, call_home_dummy2, false, "Dummy test flag to test call home");

namespace yb {

template <class ServerType, class CallHomeType>
void TestCallHome(
    const std::string& webserver_dir, const std::set<std::string>& additional_collections,
    ServerType* server) {
  std::string json;
  CountDownLatch latch(1);
  const char* tag_value = "callhome-test";

  WebserverOptions opts;
  opts.port = 0;
  opts.doc_root = webserver_dir;
  Webserver webserver(opts, "WebserverTest");
  ASSERT_OK(webserver.Start());

  std::vector<Endpoint> addrs;
  ASSERT_OK(webserver.GetBoundAddresses(&addrs));
  ASSERT_EQ(addrs.size(), 1);
  auto addr = addrs[0];

  auto handler = [&json, &latch](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
    ASSERT_EQ(req.request_method, "POST");
    ASSERT_EQ(json, req.post_data);
    latch.CountDown();
  };

  webserver.RegisterPathHandler("/callhome", "callhome", handler);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_tag) = tag_value;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_url) = Format("http://$0/callhome", addr);

  std::set<std::string> low{"cluster_uuid", "node_uuid", "server_type",
                       "timestamp",    "tablets",   "gflags"};
  low.insert(additional_collections.begin(), additional_collections.end());

  std::unordered_map<std::string, std::set<std::string>> collection_levels;
  collection_levels["low"] = low;
  collection_levels["medium"] = low;
  collection_levels["medium"].insert({"hostname", "current_user"});
  collection_levels["high"] = collection_levels["medium"];
  collection_levels["high"].insert({"metrics", "rpcs"});

  for (const auto& collection_level : collection_levels) {
    LOG(INFO) << "Collection level: " << collection_level.first;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_collection_level) = collection_level.first;
    CallHomeType call_home(server);
    json = call_home.BuildJson();
    ASSERT_TRUE(!json.empty());
    JsonReader reader(json);
    ASSERT_OK(reader.Init());
    for (const auto& field : collection_level.second) {
      LOG(INFO) << "Checking json has field: " << field;
      ASSERT_TRUE(reader.root()->HasMember(field.c_str()));
    }
    LOG(INFO) << "Checking json has field: tag";
    ASSERT_TRUE(reader.root()->HasMember("tag"));

    std::string received_tag;
    ASSERT_OK(reader.ExtractString(reader.root(), "tag", &received_tag));
    ASSERT_EQ(received_tag, tag_value);

    if (collection_level.second.find("hostname") != collection_level.second.end()) {
      std::string received_hostname;
      ASSERT_OK(reader.ExtractString(reader.root(), "hostname", &received_hostname));
      ASSERT_EQ(received_hostname, server->get_hostname());
    }

    if (collection_level.second.find("current_user") != collection_level.second.end()) {
      std::string received_user;
      ASSERT_OK(reader.ExtractString(reader.root(), "current_user", &received_user));
      auto expected_user = ASSERT_RESULT(GetLoggedInUser());
      ASSERT_EQ(received_user, expected_user);
    }

    auto count = reader.root()->MemberEnd() - reader.root()->MemberBegin();
    LOG(INFO) << "Number of elements for level " << collection_level.first << ": " << count;
    // The number of fields should be equal to the number of collectors plus one for the tag field.
    ASSERT_EQ(count, collection_level.second.size() + 1);

    call_home.SendData(json);
    ASSERT_TRUE(latch.WaitFor(MonoDelta::FromSeconds(10)));
    latch.Reset(1);

    call_home.Shutdown();
  }
}

// This tests whether the enabling/disabling of callhome is happening dynamically
// during runtime.
template <class ServerType, class CallHomeType>
void TestCallHomeFlag(const std::string& webserver_dir, ServerType* server) {
  CountDownLatch latch(1);
  const char* tag_value = "callhome-test";
  bool disabled = false;

  WebserverOptions opts;
  opts.port = 0;
  opts.doc_root = webserver_dir;
  Webserver webserver(opts, "WebserverTest");
  ASSERT_OK(webserver.Start());

  std::vector<Endpoint> addrs;
  ASSERT_OK(webserver.GetBoundAddresses(&addrs));
  ASSERT_EQ(addrs.size(), 1);
  auto addr = addrs[0];

  // By default callhome is enabled. This handler is expected to be called
  // before disabling the flag. Once the flag is disabled, there shouldn't be any http posts.
  auto handler = [&latch, &disabled](
                     const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
    ASSERT_EQ(req.request_method, "POST");
    // After callhome is disabled, assert if we get any more POST.
    ASSERT_FALSE(disabled);
    LOG(INFO) << "Received callhome data\n" << req.post_data;
    latch.CountDown();
  };

  webserver.RegisterPathHandler("/callhome", "callhome", handler);
  LOG(INFO) << "Started webserver to listen for callhome post requests.";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_tag) = tag_value;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_url) = Format("http://$0/callhome", addr);
  // Set the interval to 3 secs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_interval_secs) = 3 * kTimeMultiplier;
  // Start with the default value i.e. callhome enabled.
  disabled = false;

  CallHomeType call_home(server);
  call_home.ScheduleCallHome(1 * kTimeMultiplier);

  // Wait for at least one non-empty response.
  ASSERT_TRUE(latch.WaitFor(MonoDelta::FromSeconds(10 * kTimeMultiplier)));

  // Disable the callhome flag now.
  disabled = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_callhome_enabled) = false;
  LOG(INFO) << "Callhome disabled. No more traffic";

  // Wait for 3 cycles for no callhome posts. The handler is expected to assert
  // if it gets any new HTTP POST now.
  SleepFor(MonoDelta::FromSeconds(3 * FLAGS_callhome_interval_secs * kTimeMultiplier));

  call_home.Shutdown();
}

template <class ServerType, class CallHomeType>
void TestGFlagsCallHome(ServerType* server) {
  const auto kValueWithQuotes = R"("flagA=""String with quotes""")";
  ASSERT_OK(SET_FLAG(ysql_pg_conf_csv, kValueWithQuotes));
  const auto kHbaValue = "local all yugabyte trust";
  ASSERT_OK(SET_FLAG(ysql_hba_conf_csv, kHbaValue));
  ASSERT_OK(SET_FLAG(remote_bootstrap_from_leader_only, true));
  ASSERT_OK(SET_FLAG(TEST_call_home_dummy1, true));
  std::string json;
  CallHomeType call_home(server);
  json = call_home.BuildJson();
  ASSERT_TRUE(!json.empty());
  JsonReader reader(json);
  ASSERT_OK(reader.Init());

  LOG(INFO) << "Checking json has field: tag";
  ASSERT_TRUE(reader.root()->HasMember("gflags"));

  std::string flags;
  ASSERT_OK(reader.ExtractString(reader.root(), "gflags", &flags));
  ASSERT_STR_CONTAINS(flags, kValueWithQuotes);
  ASSERT_STR_NOT_CONTAINS(flags, kHbaValue);
  // Default AutoFlags should not be included.
  ASSERT_STR_NOT_CONTAINS(flags, "enable_automatic_tablet_splitting");
  // Non default AutoFlags should be included.
  ASSERT_STR_CONTAINS(flags, "remote_bootstrap_from_leader_only");
  // TEST flags should only be included if they are non default.
  ASSERT_STR_CONTAINS(flags, "call_home_dummy1");
  ASSERT_STR_NOT_CONTAINS(flags, "call_home_dummy2");

  call_home.Shutdown();
}

}  // namespace yb
