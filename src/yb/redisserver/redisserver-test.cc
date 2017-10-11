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

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "cpp_redis/redis_client.hpp"
#include "cpp_redis/reply.hpp"

#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/redis_table_test_base.h"

#include "yb/redisserver/redis_encoding.h"
#include "yb/redisserver/redis_server.h"

#include "yb/util/cast.h"
#include "yb/util/enums.h"
#include "yb/util/protobuf.h"
#include "yb/util/test_util.h"

DECLARE_uint64(redis_max_concurrent_commands);
DECLARE_uint64(redis_max_batch);
DECLARE_bool(redis_safe_batch);
DECLARE_bool(emulate_redis_responses);

DEFINE_uint64(test_redis_max_concurrent_commands, 20,
              "Value of redis_max_concurrent_commands for pipeline test");
DEFINE_uint64(test_redis_max_batch, 250,
              "Value of redis_max_batch for pipeline test");

METRIC_DECLARE_gauge_uint64(available_read_sessions);
METRIC_DECLARE_gauge_uint64(allocated_read_sessions);
METRIC_DECLARE_gauge_uint64(available_write_sessions);
METRIC_DECLARE_gauge_uint64(allocated_write_sessions);

using namespace std::literals; // NOLINT

namespace yb {
namespace redisserver {

using cpp_redis::RedisClient;
using cpp_redis::RedisReply;
using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::integration_tests::RedisTableTestBase;
using yb::util::ToRepeatedPtrField;
using namespace std::literals::string_literals;  // NOLINT

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

  void DoRedisTestNull(int line,
                       const std::vector<std::string>& command) {
    DoRedisTest(line, command, cpp_redis::reply::type::null,
        [line](const RedisReply& reply) {
          ASSERT_TRUE(reply.is_null()) << "Originator: " << __FILE__ << ":" << line;
        }
    );
  }

  void SyncClient() { test_client_.sync_commit(); }

  void VerifyCallbacks();

  int server_port() { return redis_server_port_; }

  CHECKED_STATUS Send(const std::string& cmd);

  CHECKED_STATUS SendCommandAndGetResponse(
      const string& cmd, int expected_resp_length, int timeout_in_millis = kDefaultTimeoutMs);

  size_t CountSessions(const GaugePrototype<uint64_t>& read_proto,
                       const GaugePrototype<uint64_t>& write_proto) {
    constexpr uint64_t kInitialValue = 0UL;
    auto read_counter = server_->metric_entity()->FindOrCreateGauge(&read_proto, kInitialValue);
    auto write_counter = server_->metric_entity()->FindOrCreateGauge(&write_proto, kInitialValue);
    return read_counter->value() + write_counter->value();
  }

  bool expected_no_sessions_ = false;

 private:
  RedisClient test_client_;
  std::atomic_int num_callbacks_called_;
  int expected_callbacks_called_;
  Socket client_sock_;
  unique_ptr<RedisServer> server_;
  int redis_server_port_ = 0;
  unique_ptr<FileLock> redis_port_lock_;
  unique_ptr<FileLock> redis_webserver_lock_;
  std::vector<uint8_t> resp_;
};

void TestRedisService::SetUp() {
  RedisTableTestBase::SetUp();

  StartServer();
  StartClient();
  num_callbacks_called_ = 0;
  expected_callbacks_called_ = 0;
  test_client_.connect("127.0.0.1", server_port(), [] (RedisClient&) {
    LOG(ERROR) << "client disconnected (disconnection handler)";
  });
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
  size_t allocated_sessions = CountSessions(METRIC_allocated_read_sessions,
                                            METRIC_allocated_write_sessions);
  if (!expected_no_sessions_) {
    ASSERT_GT(allocated_sessions, 0); // Check that metric is sane.
  } else {
    ASSERT_EQ(0, allocated_sessions);
  }
  ASSERT_EQ(allocated_sessions, CountSessions(METRIC_available_read_sessions,
                                              METRIC_available_write_sessions));

  test_client_.disconnect();
  StopClient();
  StopServer();
  RedisTableTestBase::TearDown();
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
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
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
  test_client_.send(command, [this, line, reply_type, callback] (RedisReply& reply) {
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
  const int kStringRepeats = 32 * 1024 * 1024;
  string value(kStringRepeats, 'T');
  DoRedisTestOk(__LINE__, {"SET", "foo", value});
  DoRedisTestBulkString(__LINE__, {"GET", "foo"}, value);
  DoRedisTestOk(__LINE__, {"SET", "foo", "Test"});
  DoRedisTestBulkString(__LINE__, {"GET", "foo"}, "Test");
  SyncClient();
  VerifyCallbacks();
  DoRedisTestExpectError(__LINE__, {"SET", "foo", value+value});
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
  DoRedisTestOk(__LINE__, {"SET", "k3", "v3", "EX", "10"});

  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  SyncClient();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  DoRedisTestBulkString(__LINE__, {"GET", "k1"}, "v1");
  DoRedisTestNull(__LINE__, {"GET", "k2"});
  DoRedisTestBulkString(__LINE__, {"GET", "k3"}, "v3");

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

  SyncClient();

  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"},
      {"subkey1", "41", "subkey2", "12", "subkey5", "19", "subkey6", "14"});
  DoRedisTestArray(__LINE__, {"HKEYS", "map_key"},
                   {"subkey1", "subkey2", "subkey5", "subkey6"});
  DoRedisTestArray(__LINE__, {"HVALS", "map_key"},
                   {"41", "12", "19", "14"});
  DoRedisTestInt(__LINE__, {"HLEN", "map_key"}, 4);
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
  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {"subkey1", "41", "subkey6", "14"});
  DoRedisTestInt(__LINE__, {"DEL", "map_key"}, 1); // Delete the whole map with a del
  SyncClient();

  DoRedisTestInt(__LINE__, {"EXISTS", "map_key"}, 0);
  DoRedisTestArray(__LINE__, {"HGETALL", "map_key"}, {});

  DoRedisTestInt(__LINE__, {"EXISTS", "set1"}, 0);
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
  DoRedisTestBulkString(__LINE__, {"PING"}, "pong");

  DoRedisTestOk(__LINE__, {"QUIT"});

  DoRedisTestOk(__LINE__, {"FLUSHDB"});

  SyncClient();

  VerifyCallbacks();
}

TEST_F(TestRedisService, TestDel) {
  // The default value is true, but we explicitly set this here for clarity.
  FLAGS_emulate_redis_responses = true;

  DoRedisTestOk(__LINE__, {"SET", "key", "value"});
  DoRedisTestInt(__LINE__, {"DEL", "key"}, 0);
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

}  // namespace redisserver
}  // namespace yb
