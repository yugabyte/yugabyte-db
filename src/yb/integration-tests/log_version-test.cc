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

#include <fstream>
#include <regex>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/container/small_vector.hpp>
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
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    ExternalMiniClusterITestBase::SetUpOptions(opts);
    opts.log_to_file = true;
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
  const auto logs_dir = JoinPathSegments(master->GetDataDirs()[0], "logs");
  const auto log_path = JoinPathSegments(logs_dir, BaseName(exe) + ".INFO");
  const auto fingerprint = "Application fingerprint: " + version;
  // Collect several files during log rolling for further checks.
  boost::container::small_vector<string, 5> log_files;
  auto master_proxy = cluster_->GetMasterProxy<master::MasterDdlProxy>();
  while (log_files.size() < 5) {
    // Call rpc functions to generate logs in master
    for(int i = 0; i < 20; ++i) {
      master::TruncateTableRequestPB req;
      master::TruncateTableResponsePB resp;
      rpc::RpcController rpc;
      ASSERT_OK(master_proxy.TruncateTable(req, &resp, &rpc));
    }
    auto target = ASSERT_RESULT(env_->ReadLink(log_path));
    if (log_files.empty() || target != log_files.back()) {
      log_files.push_back(std::move(target));
    }
  }
  // Rolled files are immutable, so checking them by name is race-free.
  string prev_duration_str;
  for (const auto& file : log_files) {
    const auto log_file_full_path = JoinPathSegments(logs_dir, file);
    const LogHeader header(log_file_full_path);
    ASSERT_NE(header.GetByPrefix(fingerprint), "");
    const auto& duration_line = header.GetByPrefix(kDurationPrefix);
    ASSERT_NE(duration_line, "");
    ASSERT_LT(ASSERT_RESULT(env_->GetFileSize(log_file_full_path)), 2_MB);
    const auto duration_str = duration_line.substr(kDurationPrefix.size());
    ASSERT_GE(duration_str, prev_duration_str) << "Log file durations out of order: " << file;
    prev_duration_str = duration_str;
  }
}

} // namespace test
} // namespace yb
