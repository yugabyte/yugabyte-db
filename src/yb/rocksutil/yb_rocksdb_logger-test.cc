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

#include <regex>
#include <string>

#include "yb/util/logging.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_util.h"
#include "yb/util/tostring.h"

namespace yb {

namespace {
  static constexpr char kPrefix[] = "prefix: ";
} // namespace

class YBRocksDBLoggerTest: public ::testing::Test, public google::base::Logger {
 protected:
  YBRocksDBLoggerTest() : rocksdb_logger_(kPrefix) {
  }

  void SetUp() override {
    google::InitGoogleLogging("");
    old_logger_ = google::base::GetLogger(google::GLOG_WARNING);
    google::base::SetLogger(google::GLOG_WARNING, this);
  }

  void TearDown() override {
    google::base::SetLogger(google::GLOG_WARNING, old_logger_);
  }

 public:
  void Write(bool force_flush, time_t timestamp, const char *message, int message_len) override {
    log_.append(message, message_len);
  }

  void Flush() override {
  }

  google::uint32 LogSize() override {
    return 0;
  }

  YBRocksDBLogger rocksdb_logger_;
  std::string log_;

 private:
  google::base::Logger *old_logger_;
};

TEST_F(YBRocksDBLoggerTest, LogvWithContextSmall) {
  Random r(SeedRandom());

  static constexpr size_t reserve_for_prefix = 100;
  static constexpr size_t lengths_to_test[] = {512, 1024, 32768, 65536, 131072};

  for (const size_t length : lengths_to_test) {
    const std::string text = RandomHumanReadableString(length, &r);
    RWARN(&rocksdb_logger_, "%s %d", text.c_str(), length);

    const std::string trimmed_log = util::RightTrimStr(log_);
    const size_t prefix_pos = trimmed_log.find(kPrefix);
    ASSERT_NE(prefix_pos, std::string::npos);
    const std::string log_after_prefix = trimmed_log.substr(prefix_pos + sizeof(kPrefix) - 1);
    const std::string formatted_text = text + " " + ToString(length);
    if (length > google::LogMessage::kMaxLogMessageLen - reserve_for_prefix) {
      // Log part after prefix should be a substring of what we've logged.
      ASSERT_STR_CONTAINS(formatted_text, log_after_prefix);
    } else {
      ASSERT_EQ(formatted_text, log_after_prefix);
    }

    log_.clear();
  }
}

TEST_F(YBRocksDBLoggerTest, DetailLevelPrefix) {
  // DETAIL maps to GLOG_INFO, so temporarily capture the INFO logger.
  google::base::Logger* old_info_logger = google::base::GetLogger(google::GLOG_INFO);
  google::base::SetLogger(google::GLOG_INFO, this);

  // Log line format: ...filename.cc:NNN] DETAIL: prefix: test_detail_message
  // Verify "DETAIL: " appears at the start of the message body (right after "] ").
  log_.clear();
  RLOG(rocksdb::InfoLogLevel::DETAIL_LEVEL, &rocksdb_logger_, "test_detail_message");
  ASSERT_TRUE(std::regex_search(log_, std::regex(R"(\] DETAIL: prefix: test_detail_message)")))
      << "log: " << log_;

  // Non-DETAIL message should have the prefix but no "DETAIL: " tag.
  log_.clear();
  RLOG(rocksdb::InfoLogLevel::INFO_LEVEL, &rocksdb_logger_, "test_info_message");
  ASSERT_TRUE(std::regex_search(log_, std::regex(R"(\] prefix: test_info_message)")))
      << "log: " << log_;

  google::base::SetLogger(google::GLOG_INFO, old_info_logger);
}

} // namespace yb
