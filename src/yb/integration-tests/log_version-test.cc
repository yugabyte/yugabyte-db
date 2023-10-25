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

#include <fstream>
#include <regex>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include "yb/util/logging.h"

#include "yb/client/client.h"

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_ddl.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/path_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/string_trim.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"

using std::string;

namespace yb {
namespace test {
namespace {

const string kDurationPrefix("Running duration (h:mm:ss): ");

class LogHeader {
 public:
  explicit LogHeader(const string& file_path) {
    std::ifstream log_stream(file_path);
    string line;
    for(int i = 0; i < 10 && getline(log_stream, line); ++i) {
      lines_.push_back(std::move(line));
    }
  }

  const string& GetByPrefix(const string& prefix) const {
    static const std::string empty;
    for(const auto& l : lines_) {
      if(boost::algorithm::starts_with(l, prefix)) {
        return l;
      }
    }
    return empty;
  }

 private:
  std::vector<string> lines_;
};

} // namespace

class LogRollingTest : public ExternalMiniClusterITestBase {
 public:
  void SetUpCluster(ExternalMiniClusterOptions* opts) override {
    ExternalMiniClusterITestBase::SetUpCluster(opts);
    opts->log_to_file = true;
  }
};

TEST_F(LogRollingTest, Rolling) {
  // Set minimal possible size limit for file rolling
  StartCluster({}, {"--max_log_size=1"}, 1);
  auto master = cluster_->master();
  const auto exe = master->exe();
  string version;
  ASSERT_OK(Subprocess::Call({exe, "--version"}, &version));
  version = util::TrimStr(version);
  ASSERT_TRUE(std::regex_match(
      version, std::regex(R"(version \S+ build \S+ revision \S+ build_type \S+ built at .+)")));
  const auto log_path = JoinPathSegments(master->GetDataDirs()[0], "logs", BaseName(exe) + ".INFO");
  const auto fingerprint = "Application fingerprint: " + version;
  const LogHeader header(log_path);
  ASSERT_NE(header.GetByPrefix(fingerprint), "");
  const auto& initial_duration = header.GetByPrefix(kDurationPrefix);
  ASSERT_NE(initial_duration, "");
  // In case of log rolling log_path link will be pointed to newly created file
  const auto initial_target = ASSERT_RESULT(env_->ReadLink(log_path));
  auto prev_size = ASSERT_RESULT(env_->GetFileSize(log_path));
  auto master_proxy = cluster_->GetMasterProxy<master::MasterDdlProxy>();
  while(initial_target == ASSERT_RESULT(env_->ReadLink(log_path))) {
    // Call rpc functions to generate logs in master
    for(int i = 0; i < 20; ++i) {
      master::TruncateTableRequestPB req;
      master::TruncateTableResponsePB resp;
      rpc::RpcController rpc;
      ASSERT_OK(master_proxy.TruncateTable(req, &resp, &rpc));
    }
    const auto current_size = ASSERT_RESULT(env_->GetFileSize(log_path));
    // Make sure log size is changed and it is not much than 2Mb
    // Something goes wrong in other case
    ASSERT_NE(current_size, prev_size);
    ASSERT_LT(current_size, 2_MB);
    prev_size = current_size;
  }
  const LogHeader fresh_header(log_path);
  ASSERT_NE(fresh_header.GetByPrefix(fingerprint), "");
  const auto& duration = fresh_header.GetByPrefix(kDurationPrefix);
  ASSERT_NE(duration, "");
  ASSERT_NE(duration, initial_duration);
}

} // namespace test
} // namespace yb
