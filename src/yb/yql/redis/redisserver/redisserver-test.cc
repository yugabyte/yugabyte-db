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

#include <cpp_redis/cpp_redis>

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/client/meta_cache.h"

#include "yb/integration-tests/redis_table_test_base.h"

#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/redis/redisserver/redis_encoding.h"
#include "yb/yql/redis/redisserver/redis_server.h"

#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/protobuf.h"
#include "yb/util/test_util.h"

DECLARE_uint64(redis_max_concurrent_commands);
DECLARE_uint64(redis_max_batch);
DECLARE_bool(redis_safe_batch);
DECLARE_bool(emulate_redis_responses);
DECLARE_int32(redis_max_value_size);
DECLARE_int32(redis_max_command_size);
DECLARE_int32(rpc_max_message_size);
DECLARE_int32(consensus_max_batch_size_bytes);
DECLARE_int32(consensus_rpc_timeout_ms);

DEFINE_uint64(test_redis_max_concurrent_commands, 20,
              "Value of redis_max_concurrent_commands for pipeline test");
DEFINE_uint64(test_redis_max_batch, 250,
              "Value of redis_max_batch for pipeline test");

METRIC_DECLARE_gauge_uint64(redis_available_sessions);
METRIC_DECLARE_gauge_uint64(redis_allocated_sessions);

using namespace std::literals; // NOLINT

namespace yb {
namespace redisserver {

using RedisClient = cpp_redis::client;
using RedisReply = cpp_redis::reply;
using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::integration_tests::RedisTableTestBase;
using yb::util::ToRepeatedPtrField;

#if defined(ADDRESS_SANITIZER) || defined(THREAD_SANITIZER)
constexpr int kDefaultTimeoutMs = 100000;
#else
constexpr int kDefaultTimeoutMs = 10000;
#endif

class TestRedisService : public RedisTableTestBase {
 public:
  void SetUp() override;
  void TearDown() override;

  void StartServer();
  void StopServer();
  void StartClient();
  void StopClient();
  void RestartClient();
  void SendCommandAndExpectTimeout(const string& cmd);

  void SendCommandAndExpectResponse(int line,
                                    const string& cmd,
                                    const string& resp,
                                    bool partial = false);

  void SendCommandAndExpectResponse(int line,
                                    const RefCntBuffer& cmd,
                                    const RefCntBuffer& resp,
                                    bool partial = false) {
    SendCommandAndExpectResponse(line, cmd.ToBuffer(), resp.ToBuffer(), partial);
  }

  template <class Callback>
  void DoRedisTest(int line,
                   const std::vector<std::string>& command,
                   cpp_redis::reply::type reply_type,
                   const Callback& callback);

  void DoRedisTestString(int line,
                         const std::vector<std::string>& command,
                         const std::string& expected,
                         cpp_redis::reply::type type = cpp_redis::reply::type::simple_string) {
    DoRedisTest(line, command, type,
        [line, expected](const RedisReply& reply) {
          ASSERT_EQ(expected, reply.as_string()) << "Originator: " << __FILE__ << ":" << line;
        }
    );
  }

  void DoRedisTestBulkString(int line,
                             const std::vector<std::string>& command,
                             const std::string& expected) {
    DoRedisTestString(line, command, expected, cpp_redis::reply::type::bulk_string);
  }

  void DoRedisTestOk(int line, const std::vector<std::string>& command) {
    DoRedisTestString(line, command, "OK");
  }

  void DoRedisTestExpectError(int line, const std::vector<std::string>& command) {
    DoRedisTest(line, command, cpp_redis::reply::type::error,
        [](const RedisReply& reply) {}
    );
  }

  void DoRedisTestInt(int line,
                      const std::vector<std::string>& command,
                      int expected) {
    DoRedisTest(line, command, cpp_redis::reply::type::integer,
        [line, expected](const RedisReply& reply) {
          ASSERT_EQ(expected, reply.as_integer()) << "Originator: " << __FILE__ << ":" << line;
        }
    );
  }

  // Note: expected empty string will check for null instead
  void DoRedisTestArray(int line,
      const std::vector<std::string>& command,
      const std::vector<std::string>& expected) {
    DoRedisTest(line, command, cpp_redis::reply::type::array,
        [line, expected](const RedisReply& reply) {
          const auto& replies = reply.as_array();
          ASSERT_EQ(expected.size(), replies.size()) << "Originator: " << __FILE__ << ":" << line;
          for (size_t i = 0; i < expected.size(); i++) {
            if (expected[i] == "") {
              ASSERT_TRUE(replies[i].is_null())
                  << "Originator: " << __FILE__ << ":" << line << ", i: " << i;
            } else {
              ASSERT_EQ(expected[i], replies[i].as_string())
                  << "Originator: " << __FILE__ << ":" << line << ", i: " << i;
            }
          }
        }
    );
  }

  // Used to check pairs of doubles and strings, for range scans withscores.
  void DoRedisTestScoreValueArray(int line,
                        const std::vector<std::string>& command,
                        const std::vector<double>& expected_scores,
                        const std::vector<std::string>& expected_values) {
    ASSERT_EQ(expected_scores.size(), expected_values.size());
    DoRedisTest(line, command, cpp_redis::reply::type::array,
      [line, expected_scores, expected_values](const RedisReply& reply) {
        const auto& replies = reply.as_array();
        ASSERT_EQ(expected_scores.size()*2, replies.size())
                      << "Originator: " << __FILE__ << ":" << line;
        for (size_t i = 0; i < expected_scores.size(); i++) {
          std::string::size_type sz;
          double reply_score = std::stod(replies[2 * i].as_string(), &sz);
          ASSERT_EQ(expected_scores[i], reply_score)
                        << "Originator: " << __FILE__ << ":" << line << ", i: " << i;
          ASSERT_EQ(expected_values[i], replies[2 * i + 1].as_string())
                        << "Originator: " << __FILE__ << ":" << line << ", i: " << i;
        }
      }
    );
  }

  void DoRedisTestNull(int line,
                       const std::vector<std::string>& command) {
    DoRedisTest(line, command, cpp_redis::reply::type::null,
        [line](const RedisReply& reply) {
          ASSERT_TRUE(reply.is_null()) << "Originator: " << __FILE__ << ":" << line;
        }
    );
  }

  void SyncClient() { client().sync_commit(); }

  void VerifyCallbacks();

  int server_port() { return redis_server_port_; }

  CHECKED_STATUS Send(const std::string& cmd);

  CHECKED_STATUS SendCommandAndGetResponse(
      const string& cmd, int expected_resp_length, int timeout_in_millis = kDefaultTimeoutMs);

  size_t CountSessions(const GaugePrototype<uint64_t>& proto) {
    constexpr uint64_t kInitialValue = 0UL;
    auto counter = server_->metric_entity()->FindOrCreateGauge(&proto, kInitialValue);
    return counter->value();
  }

  void TestTSTtl(const std::string& expire_command, int64_t ttl_sec, int64_t expire_val,
                 const std::string& redis_key) {
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "10", "v1", "20", "v2", "30", "v3", expire_command,
        strings::Substitute("$0", expire_val)});
    SyncClient();
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "40", "v4"});
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "10", "v5"});
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "50", "v6", expire_command,
        std::to_string(expire_val + ttl_sec)});
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "60", "v7", expire_command,
        std::to_string(expire_val - ttl_sec + kRedisMaxTtlSeconds)});
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "70", "v8", expire_command,
        std::to_string(expire_val - ttl_sec + kRedisMinTtlSeconds)});
    // Same kv with different ttl (later one should win).
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "80", "v9", expire_command,
        std::to_string(expire_val)});
    DoRedisTestOk(__LINE__, {"TSADD", redis_key, "80", "v9", expire_command,
        std::to_string(expire_val + ttl_sec)});
    SyncClient();

    // Wait for min ttl to expire.
    std::this_thread::sleep_for(std::chrono::seconds(kRedisMinTtlSeconds + 1));

    SyncClient();
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "10"}, "v5");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "20"}, "v2");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "30"}, "v3");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "50"}, "v6");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "60"}, "v7");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "80"}, "v9");
    SyncClient();

    // Wait for TTL expiry
    std::this_thread::sleep_for(std::chrono::seconds(ttl_sec + 1));
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "10"}, "v5");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "40"}, "v4");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "50"}, "v6");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "60"}, "v7");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "80"}, "v9");
    DoRedisTestNull(__LINE__, {"TSGET", redis_key, "20"});
    DoRedisTestNull(__LINE__, {"TSGET", redis_key, "30"});
    SyncClient();

    // Wait for next TTL expiry
    std::this_thread::sleep_for(std::chrono::seconds(ttl_sec + 1));
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "10"}, "v5");
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "40"}, "v4");
    DoRedisTestNull(__LINE__, {"TSGET", redis_key, "20"});
    DoRedisTestNull(__LINE__, {"TSGET", redis_key, "30"});
    DoRedisTestNull(__LINE__, {"TSGET", redis_key, "50"});
    DoRedisTestBulkString(__LINE__, {"TSGET", redis_key, "60"}, "v7");
    SyncClient();
    VerifyCallbacks();

    // Test invalid commands.
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command,
        std::to_string(expire_val - 2 * ttl_sec)}); // Negative ttl.
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", "20", "v2", "30", "v3",
        expire_command});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command, "v2", "30",
        "v3"});
    DoRedisTestExpectError(__LINE__, {"TSADD", expire_command, redis_key, "10", "v1", "30", "v3"});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command, "abc"});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command, "3.0"});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command, "123 "});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command,
        "9223372036854775808"});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command,
        "-9223372036854775809"});
    DoRedisTestExpectError(__LINE__, {"TSADD", redis_key, "10", "v1", expire_command,
        std::to_string(expire_val - ttl_sec)}); // ttl of 0 not allowed.
  }

  void TestFlush(const string& flush_cmd) {
    // Populate keys.
    const int kKeyCount = 100;
    for (int i = 0; i < kKeyCount; i++) {
      DoRedisTestOk(__LINE__, {"SET", Substitute("k$0", i),  Substitute("v$0", i)});
    }
    SyncClient();

    // Verify keys.
    for (int i = 0; i < kKeyCount; i++) {
      DoRedisTestBulkString(__LINE__, {"GET", Substitute("k$0", i)}, Substitute("v$0", i));
    }
    SyncClient();

    // Delete all keys in the database and verify keys are gone.
    DoRedisTestOk(__LINE__, {flush_cmd});
    SyncClient();
    for (int i = 0; i < kKeyCount; i++) {
      DoRedisTestNull(__LINE__, {"GET", Substitute("k$0", i)});
    }
    SyncClient();

    // Delete all keys in the database again (an NOOP) and verify there is no issue.
    DoRedisTestOk(__LINE__, {flush_cmd});
    SyncClient();
    for (int i = 0; i < kKeyCount; i++) {
      DoRedisTestNull(__LINE__, {"GET", Substitute("k$0", i)});
    }
    SyncClient();
  }

  bool expected_no_sessions_ = false;

  RedisClient& client() {
    if (!test_client_) {
      test_client_.emplace();
      test_client_->connect("127.0.0.1", server_port());
    }
    return *test_client_;
  }

 protected:
  std::string int64Max_ = std::to_string(std::numeric_limits<int64_t>::max());
  std::string int64MaxExclusive_ = "(" + int64Max_;
  std::string int64Min_ = std::to_string(std::numeric_limits<int64_t>::min());
  std::string int64MinExclusive_ = "(" + int64Min_;

 private:
  std::atomic_int num_callbacks_called_;
  int expected_callbacks_called_;
  Socket client_sock_;
  unique_ptr<RedisServer> server_;
  int redis_server_port_ = 0;
  unique_ptr<FileLock> redis_port_lock_;
  unique_ptr<FileLock> redis_webserver_lock_;
  std::vector<uint8_t> resp_;
  boost::optional<RedisClient> test_client_;
  boost::optional<google::FlagSaver> flag_saver_;
};

void TestRedisService::SetUp() {
  if (IsTsan()) {
    flag_saver_.emplace();
    FLAGS_redis_max_value_size = 1_MB;
    FLAGS_rpc_max_message_size = FLAGS_redis_max_value_size * 4 - 1;
    FLAGS_redis_max_command_size = FLAGS_rpc_max_message_size - 2_KB;
    FLAGS_consensus_max_batch_size_bytes = FLAGS_rpc_max_message_size - 2_KB;
  } else {
#ifndef NDEBUG
    flag_saver_.emplace();
    FLAGS_redis_max_value_size = 32_MB;
    FLAGS_rpc_max_message_size = FLAGS_redis_max_value_size * 4 - 1;
    FLAGS_redis_max_command_size = FLAGS_rpc_max_message_size - 2_KB;
    FLAGS_consensus_max_batch_size_bytes = FLAGS_rpc_max_message_size - 2_KB;
    FLAGS_consensus_rpc_timeout_ms = 3000;
#endif
  }

  RedisTableTestBase::SetUp();

  StartServer();
  StartClient();
  num_callbacks_called_ = 0;
  expected_callbacks_called_ = 0;
}

void TestRedisService::StartServer() {
  redis_server_port_ = GetFreePort(&redis_port_lock_);
  RedisServerOptions opts;
  opts.rpc_opts.rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", redis_server_port_);
  // No need to save the webserver port, as we don't plan on using it. Just use a unique free port.
  opts.webserver_opts.port = GetFreePort(&redis_webserver_lock_);
  string fs_root = GetTestPath("RedisServerTest-fsroot");
  opts.fs_opts.wal_paths = {fs_root};
  opts.fs_opts.data_paths = {fs_root};

  auto master_rpc_addrs = master_rpc_addresses_as_strings();
  opts.master_addresses_flag = JoinStrings(master_rpc_addrs, ",");

  server_.reset(new RedisServer(opts, nullptr /* tserver */));
  LOG(INFO) << "Starting redis server...";
  CHECK_OK(server_->Start());
  LOG(INFO) << "Redis server successfully started.";
}

void TestRedisService::StopServer() {
  LOG(INFO) << "Shut down redis server...";
  server_->Shutdown();
  server_.reset();
  LOG(INFO) << "Redis server successfully shut down.";
}

void TestRedisService::StartClient() {
  Endpoint remote(IpAddress(), server_port());
  CHECK_OK(client_sock_.Init(0));
  CHECK_OK(client_sock_.SetNoDelay(false));
  LOG(INFO) << "Connecting to " << remote;
  CHECK_OK(client_sock_.Connect(remote));
}

void TestRedisService::StopClient() { EXPECT_OK(client_sock_.Close()); }

void TestRedisService::RestartClient() {
  StopClient();
  StartClient();
}

void TestRedisService::TearDown() {
  size_t allocated_sessions = CountSessions(METRIC_redis_allocated_sessions);
  if (!expected_no_sessions_) {
    EXPECT_GT(allocated_sessions, 0); // Check that metric is sane.
  } else {
    EXPECT_EQ(0, allocated_sessions);
  }
  EXPECT_EQ(allocated_sessions, CountSessions(METRIC_redis_available_sessions));

  if (test_client_) {
    test_client_.reset();
    auto io_service = tacopie::get_default_io_service();
    tacopie::set_default_io_service(nullptr);
    EXPECT_OK(WaitFor(
        [&io_service] { return io_service.use_count() == 1; }, 60s, "Redis IO Service shutdown"));
  }
  StopClient();
  StopServer();
  RedisTableTestBase::TearDown();

  flag_saver_.reset();
}

Status TestRedisService::Send(const std::string& cmd) {
  // Send the command.
  int32_t bytes_written = 0;
  EXPECT_OK(client_sock_.Write(util::to_uchar_ptr(cmd.c_str()), cmd.length(), &bytes_written));

  EXPECT_EQ(cmd.length(), bytes_written);

  return Status::OK();
}

Status TestRedisService::SendCommandAndGetResponse(
    const string& cmd, int expected_resp_length, int timeout_in_millis) {
  RETURN_NOT_OK(Send(cmd));

  // Receive the response.
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(MonoDelta::FromMilliseconds(timeout_in_millis));
  size_t bytes_read = 0;
  resp_.resize(expected_resp_length);
  RETURN_NOT_OK(client_sock_.BlockingRecv(resp_.data(),
                                          expected_resp_length,
                                          &bytes_read,
                                          deadline));
  resp_.resize(bytes_read);
  if (expected_resp_length != bytes_read) {
    return STATUS(
        IOError, Substitute("Received $1 bytes instead of $2", bytes_read, expected_resp_length));
  }
  return Status::OK();
}

void TestRedisService::SendCommandAndExpectTimeout(const string& cmd) {
  // Don't expect to receive even 1 byte.
  ASSERT_TRUE(SendCommandAndGetResponse(cmd, 1).IsTimedOut());
}

void TestRedisService::SendCommandAndExpectResponse(int line,
                                                    const string& cmd,
                                                    const string& expected,
                                                    bool partial) {
  if (partial) {
    auto seed = GetRandomSeed32();
    std::mt19937_64 rng(seed);
    size_t last = cmd.length() - 2;
    size_t splits = std::uniform_int_distribution<size_t>(1, 10)(rng);
    std::vector<size_t> bounds(splits);
    std::generate(bounds.begin(), bounds.end(), [&rng, last]{
      return std::uniform_int_distribution<size_t>(1, last)(rng);
    });
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    size_t p = 0;
    for (auto i : bounds) {
      ASSERT_OK(Send(cmd.substr(p, i - p)));
      p = i;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_OK(SendCommandAndGetResponse(cmd.substr(p), expected.length()));
  } else {
    auto status = SendCommandAndGetResponse(cmd, expected.length());
    if (!status.ok()) {
      LOG(INFO) << "    Sent: " << Slice(cmd).ToDebugString();
      LOG(INFO) << "Received: " << Slice(resp_.data(), resp_.size()).ToDebugString();
      LOG(INFO) << "Expected: " << Slice(expected).ToDebugString();
    }
    ASSERT_OK(status);
  }

  // Verify that the response is as expected.

  std::string response(util::to_char_ptr(resp_.data()), expected.length());
  ASSERT_EQ(expected, response)
      << "Command: " << Slice(cmd).ToDebugString() << std::endl
      << "Originator: " << __FILE__ << ":" << line;
}

template <class Callback>
void TestRedisService::DoRedisTest(int line,
                                   const std::vector<std::string>& command,
                                   cpp_redis::reply::type reply_type,
                                   const Callback& callback) {
  expected_callbacks_called_++;
  VLOG(4) << "Testing with line: " << __FILE__ << ":" << line;
  client().send(command, [this, line, reply_type, callback] (RedisReply& reply) {
    VLOG(4) << "Received response for line: " << __FILE__ << ":" << line
            << " : " << reply.as_string() << ", of type: " << util::to_underlying(reply.get_type());
    num_callbacks_called_++;
    ASSERT_EQ(reply_type, reply.get_type()) << "Originator: " << __FILE__ << ":" << line;
    callback(reply);
  });
}

void TestRedisService::VerifyCallbacks() {
  ASSERT_EQ(expected_callbacks_called_, num_callbacks_called_);
}

TEST_F(TestRedisService, SimpleCommandInline) {
  SendCommandAndExpectResponse(__LINE__, "set foo bar\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, HugeCommandInline) {
  std::string value(FLAGS_redis_max_value_size, 'T');
  DoRedisTestOk(__LINE__, {"SET", "foo", value});
  SyncClient();
  DoRedisTestBulkString(__LINE__, {"GET", "foo"}, value);
  SyncClient();
  DoRedisTestOk(__LINE__, {"SET", "foo", "Test"});
  DoRedisTestBulkString(__LINE__, {"GET", "foo"}, "Test");
  SyncClient();
  DoRedisTestExpectError(__LINE__, {"SET", "foo", "Too much" + value});
  SyncClient();
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey1", value}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey2", value}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey3", value}, 1);
  SyncClient();
  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {"subkey1", value, "subkey2", value,
      "subkey3", value});
  SyncClient();
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey4", value}, 1);
  DoRedisTestExpectError(__LINE__, {"HGETALL", "map_key"});
  SyncClient();
  DoRedisTestInt(__LINE__, {"DEL", "map_key"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"DEL", "foo"}, 1);
  SyncClient();
  value[0]  = 'A';
  DoRedisTestOk(
      __LINE__, {"HMSET", "map_key1", "subkey1", value, "subkey2", value, "subkey3", value});
  SyncClient();
  DoRedisTestArray(
      __LINE__, {"HGETALL", "map_key1"}, {"subkey1", value, "subkey2", value, "subkey3", value});
  SyncClient();
  value[0] = 'B';
  DoRedisTestExpectError(
      __LINE__,
      {"HMSET", "map_key1", "subkey1", value, "subkey2", value, "subkey3", value,
          "subkey4", value});
  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, SimpleCommandMulti) {
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandsInline) {
  SendCommandAndExpectResponse(
      __LINE__,
      "set a 5\r\nset foo bar\r\nget foo\r\nget a\r\n",
      "+OK\r\n+OK\r\n$3\r\nbar\r\n$1\r\n5\r\n");
}

TEST_F(TestRedisService, BatchedCommandsInlinePartial) {
  for (int i = 0; i != 1000; ++i) {
    ASSERT_NO_FATAL_FAILURE(
        SendCommandAndExpectResponse(
            __LINE__,
            "set a 5\r\nset foo bar\r\nget foo\r\nget a\r\n",
            "+OK\r\n+OK\r\n$3\r\nbar\r\n$1\r\n5\r\n",
            /* partial */ true)
    );
  }
}

namespace {

class TestRedisServicePipelined : public TestRedisService {
 public:
  void SetUp() override {
    FLAGS_redis_safe_batch = false;
    FLAGS_redis_max_concurrent_commands = FLAGS_test_redis_max_concurrent_commands;
    FLAGS_redis_max_batch = FLAGS_test_redis_max_batch;
    TestRedisService::SetUp();
  }
};

#ifndef THREAD_SANITIZER
const size_t kPipelineKeys = 1000;
#else
const size_t kPipelineKeys = 100;
#endif

size_t ValueForKey(size_t key) {
  return key * 2;
}

std::string PipelineSetCommand() {
  std::string command;
  for (size_t i = 0; i != kPipelineKeys; ++i) {
    command += yb::Format("set $0 $1\r\n", i, ValueForKey(i));
  }
  return command;
}

std::string PipelineSetResponse() {
  std::string response;
  for (size_t i = 0; i != kPipelineKeys; ++i) {
    response += "+OK\r\n";
  }
  return response;
}

std::string PipelineGetCommand() {
  std::string command;
  for (size_t i = 0; i != kPipelineKeys; ++i) {
    command += yb::Format("get $0\r\n", i);
  }
  return command;
}

std::string PipelineGetResponse() {
  std::string response;
  for (size_t i = 0; i != kPipelineKeys; ++i) {
    std::string value = std::to_string(ValueForKey(i));
    response += yb::Format("$$$0\r\n$1\r\n", value.length(), value);
  }
  return response;
}

} // namespace

TEST_F_EX(TestRedisService, Pipeline, TestRedisServicePipelined) {
  auto start = std::chrono::steady_clock::now();
  SendCommandAndExpectResponse(__LINE__, PipelineSetCommand(), PipelineSetResponse());
  auto mid = std::chrono::steady_clock::now();
  SendCommandAndExpectResponse(__LINE__, PipelineGetCommand(), PipelineGetResponse());
  auto end = std::chrono::steady_clock::now();
  auto set_time = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
  auto get_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
  LOG(INFO) << yb::Format("Unsafe set: $0ms, get: $1ms", set_time.count(), get_time.count());
}

TEST_F_EX(TestRedisService, PipelinePartial, TestRedisServicePipelined) {
  SendCommandAndExpectResponse(__LINE__,
                               PipelineSetCommand(),
                               PipelineSetResponse(),
                               true /* partial */);
  SendCommandAndExpectResponse(__LINE__,
                               PipelineGetCommand(),
                               PipelineGetResponse(),
                               true /* partial */);
}

namespace {

class BatchGenerator {
 public:
  explicit BatchGenerator(bool collisions) : collisions_(collisions), random_(293462970) {}

  std::pair<std::string, std::string> Generate() {
    new_values_.clear();
    requested_keys_.clear();
    std::string command, response;
    size_t size = size_distribution_(random_);
    for (size_t j = 0; j != size; ++j) {
      bool get = !keys_.empty() && (bool_distribution_(random_) != 0);
      if (get) {
        int key = keys_[std::uniform_int_distribution<size_t>(0, keys_.size() - 1)(random_)];
        if (!collisions_ && new_values_.count(key)) {
          continue;
        }
        command += yb::Format("get $0\r\n", key);
        auto value = std::to_string(values_[key]);
        response += yb::Format("$$$0\r\n$1\r\n", value.length(), value);
        requested_keys_.insert(key);
      } else {
        int value = value_distribution_(random_);
        for (;;) {
          int key = key_distribution_(random_);
          if (collisions_) {
            StoreValue(key, value);
          } else if(requested_keys_.count(key) || !new_values_.emplace(key, value).second) {
            continue;
          }
          command += yb::Format("set $0 $1\r\n", key, value);
          response += "+OK\r\n";
          break;
        }
      }
    }

    for (const auto& p : new_values_) {
      StoreValue(p.first, p.second);
    }
    return std::make_pair(std::move(command), std::move(response));
  }
 private:
  void StoreValue(int key, int value) {
    auto it = values_.find(key);
    if (it == values_.end()) {
      values_.emplace(key, value);
      keys_.push_back(key);
    } else {
      it->second = value;
    }
  }

  static constexpr size_t kMinSize = 500;
  static constexpr size_t kMaxSize = kMinSize + 511;
  static constexpr int kMinKey = 0;
  static constexpr int kMaxKey = 1023;
  static constexpr int kMinValue = 0;
  static constexpr int kMaxValue = 1023;

  const bool collisions_;
  std::mt19937_64 random_;
  std::uniform_int_distribution<int> bool_distribution_{0, 1};
  std::uniform_int_distribution<size_t> size_distribution_{kMinSize, kMaxSize};
  std::uniform_int_distribution<int> key_distribution_{kMinKey, kMaxKey};
  std::uniform_int_distribution<int> value_distribution_{kMinValue, kMaxValue};

  std::unordered_map<int, int> values_;
  std::unordered_map<int, int> new_values_;
  std::unordered_set<int> requested_keys_;
  std::vector<int> keys_;
};

} // namespace

TEST_F_EX(TestRedisService, MixedBatch, TestRedisServicePipelined) {
  constexpr size_t kBatches = 50;
  BatchGenerator generator(false);
  for (size_t i = 0; i != kBatches; ++i) {
    auto batch = generator.Generate();
    SendCommandAndExpectResponse(__LINE__, batch.first, batch.second);
  }
}

class TestRedisServiceSafeBatch : public TestRedisService {
 public:
  void SetUp() override {
    FLAGS_redis_max_concurrent_commands = 1;
    FLAGS_redis_max_batch = FLAGS_test_redis_max_batch;
    FLAGS_redis_safe_batch = true;
    TestRedisService::SetUp();
  }
};

TEST_F_EX(TestRedisService, SafeMixedBatch, TestRedisServiceSafeBatch) {
  constexpr size_t kBatches = 50;
  BatchGenerator generator(true);
  std::vector<decltype(generator.Generate())> batches;
  for (size_t i = 0; i != kBatches; ++i) {
    batches.push_back(generator.Generate());
  }
  auto start = std::chrono::steady_clock::now();
  for (const auto& batch : batches) {
    SendCommandAndExpectResponse(__LINE__, batch.first, batch.second);
  }
  auto total = std::chrono::steady_clock::now() - start;
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(total).count();
  LOG(INFO) << Format("Total: $0ms, average: $1ms", ms, ms / kBatches);
}

TEST_F_EX(TestRedisService, SafeBatchPipeline, TestRedisServiceSafeBatch) {
  auto start = std::chrono::steady_clock::now();
  SendCommandAndExpectResponse(__LINE__, PipelineSetCommand(), PipelineSetResponse());
  auto mid = std::chrono::steady_clock::now();
  SendCommandAndExpectResponse(__LINE__, PipelineGetCommand(), PipelineGetResponse());
  auto end = std::chrono::steady_clock::now();
  auto set_time = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
  auto get_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
  LOG(INFO) << yb::Format("Safe set: $0ms, get: $1ms", set_time.count(), get_time.count());
}

TEST_F(TestRedisService, BatchedCommandMulti) {
  SendCommandAndExpectResponse(
      __LINE__,
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n",
      "+OK\r\n+OK\r\n+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandMultiPartial) {
  for (int i = 0; i != 1000; ++i) {
    ASSERT_NO_FATAL_FAILURE(
      SendCommandAndExpectResponse(
          __LINE__,
          "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$5\r\nTEST1\r\n"
          "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$5\r\nTEST2\r\n"
          "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$5\r\nTEST3\r\n"
          "*2\r\n$3\r\nget\r\n$3\r\nfoo\r\n",
          "+OK\r\n+OK\r\n+OK\r\n$5\r\nTEST3\r\n",
          /* partial */ true)
    );
  }
}

TEST_F(TestRedisService, IncompleteCommandInline) {
  expected_no_sessions_ = true;
  SendCommandAndExpectTimeout("TEST");
}

TEST_F(TestRedisService, MalformedCommandsFollowedByAGoodOne) {
  expected_no_sessions_ = true;
  ASSERT_NOK(SendCommandAndGetResponse("*3\r\n.1\r\n", 1));
  RestartClient();
  ASSERT_NOK(SendCommandAndGetResponse("*0\r\n.2\r\n", 1));
  RestartClient();
  ASSERT_NOK(SendCommandAndGetResponse("*-4\r\n.3\r\n", 1));
  RestartClient();
  SendCommandAndExpectResponse(__LINE__, "*2\r\n$4\r\necho\r\n$3\r\nfoo\r\n", "$3\r\nfoo\r\n");
}

namespace {

void TestBadCommand(std::string command, TestRedisService* test) {
  ASSERT_NOK(test->SendCommandAndGetResponse(command, 1)) << "Command: " << command;
  test->RestartClient();

  command.erase(std::remove(command.begin(), command.end(), '\n'), command.end());

  if (!command.empty()) {
    ASSERT_NOK(test->SendCommandAndGetResponse(command, 1)) << "Command: " << command;
    test->RestartClient();
  }
}

} // namespace

TEST_F(TestRedisService, BadCommand) {
  expected_no_sessions_ = true;

  TestBadCommand("\n", this);
  TestBadCommand(" \r\n", this);
  TestBadCommand("*\r\n9\r\n", this);
  TestBadCommand("1\r\n\r\n", this);
  TestBadCommand("1\r\n \r\n", this);
  TestBadCommand("1\r\n*0\r\n", this);
}

TEST_F(TestRedisService, BadRandom) {
  expected_no_sessions_ = true;
  const std::string allowed = " -$*\r\n0123456789";
  std::string command;
  constexpr size_t kTotalProbes = 100;
  constexpr size_t kMinCommandLength = 1;
  constexpr size_t kMaxCommandLength = 100;
  constexpr int kTimeoutInMillis = 250;
  for (int i = 0; i != kTotalProbes; ++i) {
    size_t len = RandomUniformInt(kMinCommandLength, kMaxCommandLength);
    command.clear();
    for (size_t idx = 0; idx != len; ++idx) {
      command += RandomElement(allowed);
      if (command[command.length() - 1] == '\r') {
        command += '\n';
      }
    }

    LOG(INFO) << "Command: " << command;
    auto status = SendCommandAndGetResponse(command, 1, kTimeoutInMillis);
    // We don't care about status here, because even usually it fails,
    // sometimes it has non empty response.
    // Our main goal is to test that server does not crash.
    LOG(INFO) << "Status: " << status;

    RestartClient();
  }
}

TEST_F(TestRedisService, IncompleteCommandMulti) {
  expected_no_sessions_ = true;
  SendCommandAndExpectTimeout("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTE");
}

TEST_F(TestRedisService, Echo) {
  expected_no_sessions_ = true;
  SendCommandAndExpectResponse(__LINE__, "*2\r\n$4\r\necho\r\n$3\r\nfoo\r\n", "$3\r\nfoo\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*2\r\n$4\r\necho\r\n$8\r\nfoo bar \r\n", "$8\r\nfoo bar \r\n");
  SendCommandAndExpectResponse(
      __LINE__,
      EncodeAsArray({  // The request is sent as a multi bulk array.
                        "echo"s,
                        "foo bar"s
                    }),
      EncodeAsBulkString("foo bar")  // The response is in the bulk string format.
      );
}

TEST_F(TestRedisService, TestSetOnly) {
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nset\r\n$4\r\nfool\r\n$4\r\nBEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, TestCaseInsensitiveness) {
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nSet\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nsEt\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nseT\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(
      __LINE__, "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, TestSetThenGet) {
  SendCommandAndExpectResponse(__LINE__,
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse(__LINE__, "*2\r\n$3\r\nget\r\n$3\r\nfoo\r\n", "$4\r\nTEST\r\n");
  SendCommandAndExpectResponse(
      __LINE__,
      EncodeAsArray({  // The request is sent as a multi bulk array.
                        "set"s,
                        "name"s,
                        "yugabyte"s
                    }),
      EncodeAsSimpleString("OK")  // The response is in the simple string format.
  );
  SendCommandAndExpectResponse(
      __LINE__,
      EncodeAsArray({  // The request is sent as a multi bulk array.
                        "get"s,
                        "name"s
                    }),
      EncodeAsBulkString("yugabyte")  // The response is in the bulk string format.
  );
}

TEST_F(TestRedisService, TestUsingOpenSourceClient) {
  DoRedisTestOk(__LINE__, {"SET", "hello", "42"});

  DoRedisTest(__LINE__, {"DECRBY", "hello", "12"},
      cpp_redis::reply::type::error, // TODO: fix error handling
      [](const RedisReply &reply) {
        // TBD: ASSERT_EQ(30, reply.as_integer());
      });

  DoRedisTestBulkString(__LINE__, {"GET", "hello"}, "42");
  DoRedisTestOk(__LINE__, {"SET", "world", "72"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestBinaryUsingOpenSourceClient) {
  const std::string kFooValue = "\001\002\r\n\003\004";
  const std::string kBarValue = "\013\010";

  DoRedisTestOk(__LINE__, {"SET", "foo", kFooValue});
  DoRedisTestBulkString(__LINE__, {"GET", "foo"}, kFooValue);
  DoRedisTestOk(__LINE__, {"SET", "bar", kBarValue});
  DoRedisTestBulkString(__LINE__, {"GET", "bar"}, kBarValue);

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestEmptyValue) {
  DoRedisTestOk(__LINE__, {"SET", "k1", ""});
  DoRedisTestInt(__LINE__, {"HSET", "k2", "s1", ""}, 1);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "k1"}, "");
  DoRedisTestBulkString(__LINE__, {"HGET", "k2", "s1"}, "");

  SyncClient();
  VerifyCallbacks();
}

// This test also uses the open source client
TEST_F(TestRedisService, TestTtl) {

  DoRedisTestOk(__LINE__, {"SET", "k1", "v1"});
  DoRedisTestOk(__LINE__, {"SET", "k2", "v2", "EX", "1"});
  DoRedisTestOk(__LINE__, {"SET", "k3", "v3", "EX", NonTsanVsTsan("20", "100")});
  DoRedisTestOk(__LINE__, {"SET", "k4", "v4", "EX", std::to_string(kRedisMaxTtlSeconds)});
  DoRedisTestOk(__LINE__, {"SET", "k5", "v5", "EX", std::to_string(kRedisMinTtlSeconds)});

  // Invalid ttl.
  DoRedisTestExpectError(__LINE__, {"SET", "k6", "v6", "EX",
      std::to_string(kRedisMaxTtlSeconds + 1)});
  DoRedisTestExpectError(__LINE__, {"SET", "k7", "v7", "EX",
      std::to_string(kRedisMinTtlSeconds - 1)});

  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  SyncClient();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  DoRedisTestBulkString(__LINE__, {"GET", "k1"}, "v1");
  DoRedisTestNull(__LINE__, {"GET", "k2"});
  DoRedisTestBulkString(__LINE__, {"GET", "k3"}, "v3");
  DoRedisTestBulkString(__LINE__, {"GET", "k4"}, "v4");
  DoRedisTestNull(__LINE__, {"GET", "k5"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestDummyLocal) {
  expected_no_sessions_ = true;
  DoRedisTestBulkString(__LINE__, {"INFO"}, kInfoResponse);
  DoRedisTestBulkString(__LINE__, {"INFO", "Replication"}, kInfoResponse);
  DoRedisTestBulkString(__LINE__, {"INFO", "foo", "bar", "whatever", "whatever"}, kInfoResponse);

  DoRedisTestOk(__LINE__, {"COMMAND"});
  DoRedisTestExpectError(__LINE__, {"EVAL"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestTimeSeries) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  // Need an int for timeseries as a score.
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "42.0", "42"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "12.0", "42"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "subkey1", "42"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "subkey2", "12"});
  DoRedisTestExpectError(__LINE__, {"TSGET", "ts_key", "subkey1"});
  DoRedisTestExpectError(__LINE__, {"TSGET", "ts_key", "subkey2"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", "3.0", "v3"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", "abc", "v3"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", "123abc", "v3"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", " 123", "v3"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", "0xff", "v3"});

  // Incorrect number of arguments.
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "subkey1"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "subkey2"});
  DoRedisTestExpectError(__LINE__, {"TSGET", "ts_key"});
  DoRedisTestExpectError(__LINE__, {"TSGET", "ts_key"});
  DoRedisTestExpectError(__LINE__, {"TSADD", "ts_key", "1", "v1", "2", "v2", "3"});

  // Valid statements.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "-10", "value1"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "-20", "value2"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "-30", "value3"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "10", "value4"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "20", "value5"});
  // For duplicate keys, the last one is picked up.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "30", "value100", "30", "value6"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", int64Max_, "valuemax"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", int64Min_, "valuemin"});
  SyncClient();
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key", "30", "value7"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_multi", "10", "v1", "20", "v2", "30", "v3", "40", "v4"});
  DoRedisTestOk(__LINE__, {"TSADD", "ts_multi", "10", "v5", "50", "v6", "30", "v7", "60", "v8"});
  SyncClient();
  DoRedisTestOk(__LINE__, {"TSADD", "ts_multi", "10", "v9", "70", "v10", "30", "v11", "80", "v12"});
  SyncClient();

  // Ensure we retrieve appropriate results.
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "-10"}, "value1");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "-20"}, "value2");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "-30"}, "value3");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "10"}, "value4");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "20"}, "value5");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", "30"}, "value7");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", int64Max_}, "valuemax");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_key", int64Min_}, "valuemin");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "10"}, "v9");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "20"}, "v2");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "30"}, "v11");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "40"}, "v4");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "50"}, "v6");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "60"}, "v8");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "70"}, "v10");
  DoRedisTestBulkString(__LINE__, {"TSGET", "ts_multi", "80"}, "v12");

  // Keys that are not present.
  DoRedisTestNull(__LINE__, {"TSGET", "ts_key", "40"});
  DoRedisTestNull(__LINE__, {"TSGET", "abc", "30"});

  // HGET/SISMEMBER/GET should not work with this.
  DoRedisTestNull(__LINE__, {"HGET", "ts_key", "30"});
  DoRedisTestExpectError(__LINE__, {"SISMEMBER", "ts_key", "30"});
  DoRedisTestExpectError(__LINE__, {"HEXISTS", "ts_key", "30"});
  DoRedisTestExpectError(__LINE__, {"GET", "ts_key"});

  // TSGET should not work with HSET.
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "30", "v1"}, 1);
  DoRedisTestNull(__LINE__, {"TSGET", "map_key", "30"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestSortedSets) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  // Need an double for sorted sets as a score.
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "subkey1", "42"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "subkey2", "12"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "1", "v1", "2", "v2", "abc", "v3"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "1", "v1", "2", "v2", "123abc", "v3"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "1", "v1", "2", "v2", " 123", "v3"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "1", " 2"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "1", "abc"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "abc", "2"});


  // Incorrect number of arguments.
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "subkey1"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "1", "v1", "2", "v2", "3"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "1"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "1", "2", "3"});
  DoRedisTestExpectError(__LINE__, {"ZRANGEBYSCORE", "z_key", "1", "2", "WITHSCORES", "abc"});
  DoRedisTestExpectError(__LINE__, {"ZREM", "z_key"});

  // Valid statements
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "-30.0", "v1"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "-20.0", "v2"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "-10.0", "v3"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "10.0", "v4"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "20.0", "v5"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "30.0", "v6"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key",
      strings::Substitute("$0", std::numeric_limits<double>::max()), "vmax"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key",
      strings::Substitute("$0",  -std::numeric_limits<double>::max()), "vmin"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "40.0", "v6"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "0x1e", "v6"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "-20", "v1"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "-30", "v1"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "30.000001", "v7"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "30.000001", "v8"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZCARD", "z_key"}, 10);

  DoRedisTestInt(__LINE__, {"ZADD", "z_multi", "-10.0", "v3", "-20.0", "v2", "-30.0", "v1",
      "10.0", "v4", "20.0", "v5", "30.0", "v6"}, 6);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_multi", "40.0", "v6", "0x1e", "v6", "-20", "v1", "-30", "v1",
      "30.000001", "v7", "30.000001", "v8"}, 2);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_multi",
      strings::Substitute("$0", std::numeric_limits<double>::max()), "vmax",
      strings::Substitute("$0", -std::numeric_limits<double>::max()), "vmin"}, 2);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZCARD", "z_multi"}, 10);

  // Ensure we retrieve appropriate results.
  LOG(INFO) << "Starting ZRANGE queries";
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "+inf", "-inf"}, {});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "-inf", "+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "vmax"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "(-inf", "(+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "vmax"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "20.0", "30.0"}, {"v5", "v6"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "20.0", "30.000001"},
                   {"v5", "v6", "v7", "v8"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "20.0", "(30.000001"}, {"v5", "v6"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "(20.0", "30.000001"}, {"v6", "v7", "v8"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "-20.0", "-10.0"}, {"v2", "v3"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "(-20.0", "(-10.0"}, {});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "+inf", "-inf"}, {});

  DoRedisTestScoreValueArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "20.0", "30.0", "WITHSCORES"},
                             {20.0, 30.0}, {"v5", "v6"});
  DoRedisTestScoreValueArray(__LINE__,
                             {"ZRANGEBYSCORE", "z_key", "20.0", "30.000001", "withscores"},
                             {20.0, 30.0, 30.000001, 30.000001}, {"v5", "v6", "v7", "v8"});


  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "-inf", "+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "vmax"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "(-inf", "(+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "vmax"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "20.0", "30.0"}, {"v5", "v6"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "20.0", "30.000001"},
                   {"v5", "v6", "v7", "v8"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "20.0", "(30.000001"}, {"v5", "v6"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "(20.0", "30.000001"},
                   {"v6", "v7", "v8"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "-20.0", "-10.0"}, {"v2", "v3"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "(-20.0", "(-10.0"}, {});

  DoRedisTestScoreValueArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "20.0", "30.0", "WITHSCORES"},
                             {20.0, 30.0}, {"v5", "v6"});
  DoRedisTestScoreValueArray(__LINE__,
                             {"ZRANGEBYSCORE", "z_multi", "20.0", "30.000001", "withscores"},
                             {20.0, 30.0, 30.000001, 30.000001}, {"v5", "v6", "v7", "v8"});

  DoRedisTestInt(__LINE__, {"ZREM", "z_key", "v6"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZREM", "z_key", "v6"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZREM", "z_key", "v7"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZREM", "z_key", "v9"}, 0);
  SyncClient();

  DoRedisTestInt(__LINE__, {"ZREM", "z_multi", "v6", "v7", "v9"}, 2);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZREM", "z_multi", "v6", "v7", "v9"}, 0);
  SyncClient();

  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "-inf", "+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v8", "vmax"});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_key"}, 8);

  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_multi", "-inf", "+inf"},
                   {"vmin", "v1", "v2", "v3", "v4", "v5", "v8", "vmax"});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_multi"}, 8);

  // Test NX/CH option.
  LOG(INFO) << "Starting ZADD with options";
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "NX", "0", "v8"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "NX", "CH", "0", "v8"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "NX", "0", "v9"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "NX", "40", "v9"}, 0);
  SyncClient();

  // Make sure that only v9 exists at 0 and not at 40.
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "0.0", "0.0"}, {"v9"});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "40.0", "40.0"}, {});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_key"}, 9);

  // Test XX/CH option.
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "XX", "CH", "0", "v8"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "XX", "30.000001", "v8"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "XX", "0", "v10"}, 0);
  SyncClient();

  // Make sure that only v9 exists at 0 and v8 exists at 30.000001.
  DoRedisTestScoreValueArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "0.0", "0.0", "WITHSCORES"},
                             {0.0}, {"v9"});
  DoRedisTestScoreValueArray(__LINE__,
                             {"ZRANGEBYSCORE", "z_key", "30.000001", "30.000001", "WITHSCORES"},
                             {30.000001}, {"v8"});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_key"}, 9);

  // Test NX/XX/CH option for multi.
  DoRedisTestInt(__LINE__, {"ZADD", "z_multi", "NX", "0", "v8", "40", "v9"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_multi", "CH", "0", "v8", "0", "v9"}, 2);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_multi", "XX", "CH", "30.000001", "v8", "0", "v10"}, 1);
  SyncClient();

  // Make sure that only v9 exists and 0 and v8 exists at 30.000001.
  DoRedisTestScoreValueArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "0.0", "0.0", "WITHSCORES"},
                             {0.0}, {"v9"});
  DoRedisTestScoreValueArray(__LINE__,
                             {"ZRANGEBYSCORE", "z_key", "30.000001", "30.000001", "WITHSCORES"},
                             {30.000001}, {"v8"});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_multi"}, 9);

  // Test incr option.
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "INCR", "10", "v8"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "INCR", "XX", "CH", "10", "v8"}, 1);
  SyncClient();
  // This shouldn't do anything, since NX option is specified.
  DoRedisTestInt(__LINE__, {"ZADD", "z_key", "INCR", "NX", "10", "v8"}, 0);
  SyncClient();

  // Make sure v8 has been incremented by 20.
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "30.000001", "30.000001"}, {});
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "z_key", "50.000001", "50.000001"}, {"v8"});
  DoRedisTestInt(__LINE__, {"ZCARD", "z_key"}, 9);


  // HGET/SISMEMBER/GET/TS should not work with this.
  DoRedisTestExpectError(__LINE__, {"SISMEMBER", "z_key", "30"});
  DoRedisTestExpectError(__LINE__, {"HEXISTS", "z_key", "30"});
  DoRedisTestExpectError(__LINE__, {"GET", "z_key"});
  DoRedisTestExpectError(__LINE__, {"TSRANGE", "z_key", "1", "a"});


  // ZADD should not work with HSET.
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "30", "v1"}, 1);
  DoRedisTestExpectError(__LINE__, {"ZADD", "map_key", "40", "v2"});

  // Cannot have both NX and XX options.
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "CH", "NX", "XX", "0", "v1"});

  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "CH", "NX", "INCR"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "XX"});
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "CH", "NX", "0", "v1", "1"});
  // Cannot have incr with multiple score value pairs.
  DoRedisTestExpectError(__LINE__, {"ZADD", "z_key", "INCR", "0", "v1", "1", "v2"});

  // Test ZREM on non-existent key and then add the same key.
  DoRedisTestInt(__LINE__, {"ZREM", "my_z_set", "v1"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZCARD", "my_z_set"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZADD", "my_z_set", "1", "v1"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"ZCARD", "my_z_set"}, 1);
  SyncClient();
  DoRedisTestArray(__LINE__, {"ZRANGEBYSCORE", "my_z_set", "1", "1"}, {"v1"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestTimeSeriesTTL) {
  int64_t ttl_sec = 5;
  TestTSTtl("EXPIRE_IN", ttl_sec, ttl_sec, "test_expire_in");
  int64_t curr_time_sec = GetCurrentTimeMicros() / MonoTime::kMicrosecondsPerSecond;
  TestTSTtl("EXPIRE_AT", ttl_sec, curr_time_sec + ttl_sec, "test_expire_at");

  DoRedisTestExpectError(__LINE__, {"TSADD", "test_expire_in", "10", "v1", "EXPIRE_IN",
      std::to_string(kRedisMinTtlSeconds - 1)});
  DoRedisTestExpectError(__LINE__, {"TSADD", "test_expire_in", "10", "v1", "EXPIRE_IN",
      std::to_string(kRedisMaxTtlSeconds + 1)});

  DoRedisTestExpectError(__LINE__, {"TSADD", "test_expire_at", "10", "v1", "EXPIRE_AT",
      std::to_string(curr_time_sec + kRedisMinTtlSeconds - 1)});
  DoRedisTestExpectError(__LINE__, {"TSADD", "test_expire_at", "10", "v1", "EXPIRE_IN",
      std::to_string(curr_time_sec + kRedisMaxTtlSeconds + 1)});
}

  TEST_F(TestRedisService, TestTsRangeByTime) {
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "-50", "v1",
      "-40", "v2",
      "-30", "v3",
      "-20", "v4",
      "-10", "v5",
      "10", "v6",
      "20", "v7",
      "30", "v8",
      "40", "v9",
      "50", "v10",
  });

  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-35", "25"},
                   {"-30", "v3", "-20", "v4", "-10", "v5", "10", "v6", "20", "v7"});

  // Overwrite and test.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "-50", "v11",
      "-40", "v22",
      "-30", "v33",
      "-20", "v44",
      "-10", "v55",
      "10", "v66",
      "20", "v77",
      "30", "v88",
      "40", "v99",
      "50", "v110",
  });

  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-55", "-10"},
                   {"-50", "v11", "-40", "v22", "-30", "v33", "-20", "v44", "-10", "v55"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-20", "55"},
                   {"-20", "v44", "-10", "v55", "10", "v66", "20", "v77", "30", "v88", "40", "v99",
                       "50", "v110"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-55", "55"},
                   {"-50", "v11", "-40", "v22", "-30", "v33", "-20", "v44", "-10", "v55",
                       "10", "v66", "20", "v77", "30", "v88", "40", "v99", "50", "v110"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-15", "-5"}, {"-10", "v55"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "10"}, {"10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-10", "-10"}, {"-10", "v55"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-57", "-55"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "55", "60"}, {});

  // Test with ttl.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "-30", "v333",
      "-10", "v555",
      "20", "v777",
      "30", "v888",
      "50", "v1110",
      "EXPIRE_IN", "5",
  });
  SyncClient();
  std::this_thread::sleep_for(std::chrono::seconds(6));
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-55", "-10"},
                   {"-50", "v11", "-40", "v22", "-20", "v44"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-20", "55"},
                   {"-20", "v44", "10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-55", "60"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-15", "-5"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "10"}, {"10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-25", "-15"}, {"-20", "v44"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-5", "-15"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-45", "-55"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "45", "55"}, {});

  // Test exclusive ranges.
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-20", "(40"}, {"10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-20", "(-20"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-20", "-10"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-10", "(10"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-50", "(-40"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-55", "(11"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-50", "10"},
                   {"-40", "v22", "-20", "v44", "10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-51", "10"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66"});

  // Test infinity.
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-10", "+inf"},
                   {"10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-inf", "10"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-10", "(+inf"},
                   {"10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-inf", "10"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-inf", "+inf"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "(-inf", "(+inf"},
                   {"-50", "v11", "-40", "v22", "-20", "v44", "10", "v66", "40", "v99"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "+inf", "-inf"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "+inf", "10"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "+inf", "+inf"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "-inf"}, {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "-inf", "-inf"}, {});
  SyncClient();

  // Test infinity with int64 min, max.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_inf",
      int64Min_, "v1",
      "-10", "v2",
      "10", "v3",
      int64Max_, "v4",
  });
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "-inf", "+inf"},
                   {int64Min_, "v1", "-10", "v2", "10", "v3", int64Max_, "v4"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "(-inf", "(+inf"},
                   {int64Min_, "v1", "-10", "v2", "10", "v3", int64Max_, "v4"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "-inf", "-inf"},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "+inf", "+inf"},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "-10", "(+inf"},
                   {"-10", "v2", "10", "v3", int64Max_, "v4"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "-10", "+inf"},
                   {"-10", "v2", "10", "v3", int64Max_, "v4"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "(-inf", "10"},
                   {int64Min_, "v1", "-10", "v2", "10", "v3"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", "-inf", "10"},
                   {int64Min_, "v1", "-10", "v2", "10", "v3"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64Min_, int64Max_},
                   {int64Min_, "v1", "-10", "v2", "10", "v3", int64Max_, "v4"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64MaxExclusive_, int64Max_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64MaxExclusive_, int64MaxExclusive_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64Max_, int64MaxExclusive_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64MinExclusive_, int64MinExclusive_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64MinExclusive_, int64Min_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64Min_, int64MinExclusive_},
                   {});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64Min_, int64Min_},
                   {int64Min_, "v1"});
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_inf", int64Max_, int64Max_},
                   {int64Max_, "v4"});

  // Test invalid requests.
  DoRedisTestExpectError(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "20", "30"});
  DoRedisTestExpectError(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "abc"});
  DoRedisTestExpectError(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "20.1"});
  DoRedisTestOk(__LINE__, {"HMSET", "map_key",
      "1", "v100",
      "2", "v200",
      "3", "v300",
      "4", "v400",
      "5", "v500",
  });
  DoRedisTestOk(__LINE__, {"HMSET", "map_key",
      "6", "v600"
  });
  DoRedisTestOk(__LINE__, {"SET", "key", "value"});
  SyncClient();
  DoRedisTestExpectError(__LINE__, {"TSRANGEBYTIME" , "map_key", "1", "5"});
  DoRedisTestExpectError(__LINE__, {"TSRANGEBYTIME" , "key"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestTsRem) {

  // Try some deletes before inserting any data.
  DoRedisTestOk(__LINE__, {"TSREM", "invalid_key", "20", "40", "70", "90"});

  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "10", "v1",
      "20", "v2",
      "30", "v3",
      "40", "v4",
      "50", "v5",
      "60", "v6",
      "70", "v7",
      "80", "v8",
      "90", "v9",
      "100", "v10",
  });

  // Try some deletes.
  SyncClient();
  DoRedisTestOk(__LINE__, {"TSREM", "ts_key", "20", "40", "70", "90"});
  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "100"},
                   {"10", "v1", "30", "v3", "50", "v5", "60", "v6", "80", "v8", "100", "v10"});
  DoRedisTestOk(__LINE__, {"TSREM", "ts_key", "30", "60", "70", "80", "90"});
  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "100"},
                   {"10", "v1", "50", "v5", "100", "v10"});

  // Now add some data and try some more deletes.
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "25", "v25",
      "35", "v35",
      "45", "v45",
      "55", "v55",
      "75", "v75",
      "85", "v85",
      "95", "v95",
  });
  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "100"},
                   {"10", "v1", "25", "v25", "35", "v35", "45", "v45", "50", "v5", "55", "v55",
                       "75", "v75", "85", "v85", "95", "v95", "100", "v10"});
  DoRedisTestOk(__LINE__, {"TSREM", "ts_key", "10", "25", "30", "45", "50", "65", "70", "85",
      "90"});
  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "100"},
                   {"35", "v35", "55", "v55", "75", "v75", "95", "v95", "100", "v10"});

  // Delete top level, then add some values and verify.
  DoRedisTestInt(__LINE__, {"DEL", "ts_key"}, 1);
  SyncClient();
  DoRedisTestOk(__LINE__, {"TSADD", "ts_key",
      "22", "v22",
      "33", "v33",
      "44", "v44",
      "55", "v55",
      "77", "v77",
      "88", "v88",
      "99", "v99",
  });
  SyncClient();
  DoRedisTestArray(__LINE__, {"TSRANGEBYTIME" , "ts_key", "10", "100"},
                   {"22", "v22", "33", "v33", "44", "v44", "55", "v55", "77", "v77", "88", "v88",
                       "99", "v99"});

  // Now try invalid commands.
  DoRedisTestExpectError(__LINE__, {"TSREM", "ts_key"}); // Not enough arguments.
  DoRedisTestExpectError(__LINE__, {"TSREM", "ts_key", "v1", "10"}); // wrong type for timestamp.
  DoRedisTestExpectError(__LINE__, {"TSREM", "ts_key", "1.0", "10"}); // wrong type for timestamp.
  DoRedisTestExpectError(__LINE__, {"HDEL", "ts_key", "22"}); // wrong delete type.
  DoRedisTestOk(__LINE__, {"HMSET", "hkey", "10", "v1", "20", "v2"});
  DoRedisTestExpectError(__LINE__, {"TSREM", "hkey", "10", "20"}); // wrong delete type.


  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestOverwrites) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  // Test Upsert.
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey1", "42"}, 1);
  DoRedisTestBulkString(__LINE__, {"HGET", "map_key", "subkey1"}, "42");
  // Overwrite the same key. Using Set.
  DoRedisTestOk(__LINE__, {"SET", "map_key", "new_value"});
  DoRedisTestBulkString(__LINE__, {"GET", "map_key"}, "new_value");
  SyncClient();

  // Test NX.
  DoRedisTestOk(__LINE__, {"SET", "key", "value1", "NX"});
  DoRedisTestBulkString(__LINE__, {"GET", "key"}, "value1");
  DoRedisTestNull(__LINE__, {"SET", "key", "value2", "NX"});
  DoRedisTestBulkString(__LINE__, {"GET", "key"}, "value1");

  // Test XX.
  DoRedisTestOk(__LINE__, {"SET", "key", "value2", "XX"});
  DoRedisTestBulkString(__LINE__, {"GET", "key"}, "value2");
  DoRedisTestOk(__LINE__, {"SET", "key", "value3", "XX"});
  DoRedisTestBulkString(__LINE__, {"GET", "key"}, "value3");
  DoRedisTestNull(__LINE__, {"SET", "unknown_key", "value", "XX"});

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestAdditionalCommands) {

  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey1", "42"}, 1);
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey2", "12"}, 1);

  SyncClient();

  // With emulate_redis_responses flag = true, we expect an int response 0 because the subkey
  // already existed. If flag is false, we'll get an OK response, which is tested later.
  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey1", "41"}, 0);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"HGET", "map_key", "subkey1"}, "41");

  DoRedisTestArray(__LINE__, {"HMGET", "map_key", "subkey1", "subkey3", "subkey2"},
      {"41", "", "12"});

  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {"subkey1", "41", "subkey2", "12"});

  DoRedisTestOk(__LINE__, {"SET", "key1", "30"});

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GETSET", "key1", "val1"}, "30");
  DoRedisTestNull(__LINE__, {"GETSET", "non_existent", "val2"});

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "key1"}, "val1");
  DoRedisTestInt(__LINE__, {"APPEND", "key1", "extra1"}, 10);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "key1"}, "val1extra1");

  DoRedisTestNull(__LINE__, {"GET", "key2"});
  // Deleting an empty key should return 0
  DoRedisTestInt(__LINE__, {"DEL", "key2"}, 0);
  // Appending to an empty key should work
  DoRedisTestInt(__LINE__, {"APPEND", "key2", "val2"}, 4);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "key2"}, "val2");

  SyncClient();

  DoRedisTestInt(__LINE__, {"DEL", "key2"}, 1);

  SyncClient();

  DoRedisTestNull(__LINE__, {"GET", "key2"});
  DoRedisTestInt(__LINE__, {"SETRANGE", "key1", "2", "xyz3"}, 10);
  DoRedisTestInt(__LINE__, {"SETRANGE", "sr1", "2", "abcd"}, 6);
  DoRedisTestBulkString(__LINE__, {"GET", "sr1"}, "\0\0abcd"s);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "key1"}, "vaxyz3tra1");
  DoRedisTestOk(__LINE__, {"SET", "key3", "23"});

  SyncClient();

  DoRedisTestInt(__LINE__, {"INCR", "key3"}, 24);

  SyncClient();

  DoRedisTestBulkString(__LINE__, {"GET", "key3"}, "24");

  DoRedisTestInt(__LINE__, {"STRLEN", "key1"}, 10);
  DoRedisTestInt(__LINE__, {"STRLEN", "key2"}, 0);
  DoRedisTestInt(__LINE__, {"STRLEN", "key3"}, 2);

  DoRedisTestInt(__LINE__, {"EXISTS", "key1"}, 1);
  DoRedisTestInt(__LINE__, {"EXISTS", "key2"}, 0);
  DoRedisTestInt(__LINE__, {"EXISTS", "key3"}, 1);
  DoRedisTestInt(__LINE__, {"EXISTS", "map_key"}, 1);
  DoRedisTestBulkString(__LINE__, {"GETRANGE", "key1", "1", "-1"}, "axyz3tra1");

  DoRedisTestOk(__LINE__, {"HMSET", "map_key", "subkey5", "19", "subkey6", "14"});
  // The last value for a duplicate key is picked up.
  DoRedisTestOk(__LINE__, {"HMSET", "map_key", "hashkey1", "v1", "hashkey2", "v2",
      "hashkey1", "v3"});

  SyncClient();

  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"},
      {"hashkey1", "v3", "hashkey2", "v2", "subkey1", "41", "subkey2", "12", "subkey5", "19",
          "subkey6", "14"});
  DoRedisTestArray(__LINE__, {"HKEYS", "map_key"},
                   {"hashkey1", "hashkey2", "subkey1", "subkey2", "subkey5", "subkey6"});
  DoRedisTestArray(__LINE__, {"HVALS", "map_key"},
                   {"v3", "v2", "41", "12", "19", "14"});
  DoRedisTestInt(__LINE__, {"HLEN", "map_key"}, 6);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey1"}, 1);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey2"}, 1);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey3"}, 0);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey4"}, 0);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey5"}, 1);
  DoRedisTestInt(__LINE__, {"HEXISTS", "map_key", "subkey6"}, 1);
  // HSTRLEN
  DoRedisTestInt(__LINE__, {"HSTRLEN", "map_key", "subkey1"}, 2);
  DoRedisTestInt(__LINE__, {"HSTRLEN", "map_key", "does_not_exist"}, 0);
  SyncClient();

  // HDEL
  // subkey7 doesn't exists
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "subkey2", "subkey7", "subkey5"}, 2);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "subkey9"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"EXISTS", "map_key"}, 1);
  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {"hashkey1", "v3", "hashkey2", "v2",
      "subkey1", "41", "subkey6", "14"});
  DoRedisTestInt(__LINE__, {"DEL", "map_key"}, 1); // Delete the whole map with a del
  SyncClient();

  DoRedisTestInt(__LINE__, {"EXISTS", "map_key"}, 0);
  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {});

  DoRedisTestInt(__LINE__, {"EXISTS", "set1"}, 0);
  DoRedisTestInt(__LINE__, {"SADD", "set1", "val1"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"DEL", "set1"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SADD", "set1", "val1"}, 1);
  DoRedisTestInt(__LINE__, {"SADD", "set2", "val5", "val5", "val5"}, 1);
  DoRedisTestInt(__LINE__, {"EXISTS", "set1"}, 1);

  SyncClient();

  DoRedisTestInt(__LINE__, {"SADD", "set1", "val2", "val1", "val3"}, 2);

  SyncClient();

  DoRedisTestArray(__LINE__, {"SMEMBERS", "set1"}, {"val1", "val2", "val3"});
  DoRedisTestInt(__LINE__, {"SCARD", "set1"}, 3);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "val1"}, 1);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "val2"}, 1);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "val3"}, 1);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "val4"}, 0);
  SyncClient();

  // SREM remove val1 and val3. val4 doesn't exist.
  DoRedisTestInt(__LINE__, {"SREM", "set1", "val1", "val3", "val4"}, 2);
  SyncClient();
  DoRedisTestArray(__LINE__, {"SMEMBERS", "set1"}, {"val2"});

  // AUTH/CONFIG should be dummy implementations, that respond OK irrespective of the arguments
  DoRedisTestOk(__LINE__, {"AUTH", "foo", "subkey5", "19", "subkey6", "14"});
  DoRedisTestOk(__LINE__, {"AUTH"});
  DoRedisTestOk(__LINE__, {"CONFIG", "foo", "subkey5", "19", "subkey6", "14"});
  DoRedisTestOk(__LINE__, {"CONFIG"});
  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  SyncClient();

  DoRedisTest(__LINE__, {"ROLE"}, cpp_redis::reply::type::array,
      [](const RedisReply& reply) {
        const auto& replies = reply.as_array();
        ASSERT_EQ(3, replies.size());
        ASSERT_EQ("master", replies[0].as_string());
        ASSERT_EQ(0, replies[1].as_integer());
        ASSERT_TRUE(replies[2].is_array());
        ASSERT_EQ(0, replies[2].as_array().size());
      }
  );

  DoRedisTestBulkString(__LINE__, {"PING", "foo"}, "foo");
  DoRedisTestBulkString(__LINE__, {"PING"}, "PONG");

  DoRedisTestOk(__LINE__, {"QUIT"});

  DoRedisTestOk(__LINE__, {"FLUSHDB"});

  SyncClient();

  VerifyCallbacks();
}

TEST_F(TestRedisService, TestDel) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  DoRedisTestOk(__LINE__, {"SET", "key", "value"});
  DoRedisTestInt(__LINE__, {"DEL", "key"}, 1);
  DoRedisTestInt(__LINE__, {"DEL", "key"}, 0);
  DoRedisTestInt(__LINE__, {"DEL", "non_existent"}, 0);
  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestHDel) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  DoRedisTestInt(__LINE__, {"HSET", "map_key", "subkey1", "42"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "subkey1", "non_existent_1", "non_existent_2"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "non_existent_1"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "non_existent_1", "non_existent_2"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"HDEL", "map_key", "non_existent_1", "non_existent_1"}, 0);
  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestSADDBatch) {
  DoRedisTestInt(__LINE__, {"SADD", "set1", "10"}, 1);
  DoRedisTestInt(__LINE__, {"SADD", "set1", "20"}, 1);
  DoRedisTestInt(__LINE__, {"SADD", "set1", "30"}, 1);
  DoRedisTestInt(__LINE__, {"SADD", "set1", "30"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "10"}, 1);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "20"}, 1);
  DoRedisTestInt(__LINE__, {"SISMEMBER", "set1", "30"}, 1);
  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestSRem) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  DoRedisTestInt(__LINE__, {"SADD", "set_key", "subkey1"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SREM", "set_key", "subkey1", "non_existent_1", "non_existent_2"}, 1);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SREM", "set_key", "non_existent_1"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SREM", "set_key", "non_existent_1", "non_existent_2"}, 0);
  SyncClient();
  DoRedisTestInt(__LINE__, {"SREM", "set_key", "non_existent_1", "non_existent_1"}, 0);
  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestEmulateFlagFalse) {
  FLAGS_emulate_redis_responses = false;

  DoRedisTestOk(__LINE__, {"HSET", "map_key", "subkey1", "42"});

  DoRedisTestOk(__LINE__, {"SADD", "set_key", "val1", "val2", "val1"});

  DoRedisTestOk(__LINE__, {"HDEL", "map_key", "subkey1", "subkey2"});

  SyncClient();

  VerifyCallbacks();

}

TEST_F(TestRedisService, TestQuit) {
  DoRedisTestOk(__LINE__, {"SET", "key", "value"});
  DoRedisTestBulkString(__LINE__, {"GET", "key"}, "value");
  DoRedisTestInt(__LINE__, {"DEL", "key"}, 1);
  DoRedisTestOk(__LINE__, {"QUIT"});
  SyncClient();
  VerifyCallbacks();
  // Connection closed so following command fails
  DoRedisTestExpectError(__LINE__, {"SET", "key", "value"});
}

TEST_F(TestRedisService, TestFlushAll) {
  TestFlush("FLUSHALL");
}

TEST_F(TestRedisService, TestFlushDb) {
  TestFlush("FLUSHDB");
}

}  // namespace redisserver
}  // namespace yb
