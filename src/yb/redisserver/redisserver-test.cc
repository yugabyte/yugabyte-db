// Copyright (c) YugaByte, Inc.

#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "cpp_redis/redis_client.hpp"
#include "cpp_redis/reply.hpp"

#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/redis_table_test_base.h"

#include "yb/redisserver/redis_encoding.h"
#include "yb/redisserver/redis_server.h"

#include "yb/util/cast.h"
#include "yb/util/test_util.h"

DECLARE_uint64(redis_max_concurrent_commands);

DEFINE_uint64(test_redis_max_concurrent_commands, 20,
              "Value of redis_max_concurrent_commands for pipeline test");

namespace yb {
namespace redisserver {

using cpp_redis::RedisClient;
using cpp_redis::RedisReply;
using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::integration_tests::RedisTableTestBase;

class TestRedisService : public RedisTableTestBase {
 public:
  void SetUp() override;
  void TearDown() override;

 protected:
  void StartServer();
  void StopServer();
  void StartClient();
  void StopClient();
  void RestartClient();
  void SendCommandAndExpectTimeout(const string& cmd);

  void SendCommandAndExpectResponse(const string& cmd, const string& resp, bool partial = false);

  void DoRedisTest(vector<string> command,
                   cpp_redis::reply::type reply_type,
                   void(*callback)(const RedisReply& reply));

  void SyncClient() { test_client_.sync_commit(); }

  void VerifyCallbacks();

  int server_port() { return redis_server_port_; }

  Status Send(const std::string& cmd);

  Status SendCommandAndGetResponse(
      const string& cmd, int expected_resp_length, int timeout_in_millis = 10000);

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
  redis_server_port_ = GetFreePort(&redis_port_lock_);
  opts.rpc_opts.rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", redis_server_port_);
  // No need to save the webserver port, as we don't plan on using it. Just use a unique free port.
  opts.webserver_opts.port = GetFreePort(&redis_webserver_lock_);
  string fs_root = GetTestPath("RedisServerTest-fsroot");
  opts.fs_opts.wal_paths = {fs_root};
  opts.fs_opts.data_paths = {fs_root};

  auto master_rpc_addrs = master_rpc_addresses_as_strings();
  opts.master_addresses_flag = JoinStrings(master_rpc_addrs, ",");

  server_.reset(new RedisServer(opts));
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



void TestRedisService::SendCommandAndExpectResponse(const string& cmd,
                                                    const string& resp,
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
    ASSERT_OK(SendCommandAndGetResponse(cmd.substr(p), resp.length()));
  } else {
    ASSERT_OK(SendCommandAndGetResponse(cmd, resp.length()));
  }

  // Verify that the response is as expected.

  ASSERT_EQ(resp, string(util::to_char_ptr(resp_.data()), resp.length()));
}

void TestRedisService::DoRedisTest(vector<string> command,
                                   cpp_redis::reply::type reply_type,
                                   void(*callback)(const RedisReply& reply)) {
  expected_callbacks_called_++;
  test_client_.send(command, [this, reply_type, callback] (RedisReply& reply) {
    LOG(INFO) << "Received response : " << reply.as_string();
    num_callbacks_called_++;
    ASSERT_EQ(reply_type, reply.get_type());
    callback(reply);
  });
}

void TestRedisService::VerifyCallbacks() {
  ASSERT_EQ(expected_callbacks_called_, num_callbacks_called_);
}

TEST_F(TestRedisService, SimpleCommandInline) {
  SendCommandAndExpectResponse("set foo bar\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, SimpleCommandMulti) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandsInline) {
  SendCommandAndExpectResponse(
      "set a 5\r\nset foo bar\r\nget foo\r\nget a\r\n", "+OK\r\n+OK\r\n$3\r\nbar\r\n$1\r\n5\r\n");
}

TEST_F(TestRedisService, BatchedCommandsInlinePartial) {
  for (int i = 0; i != 1000; ++i) {
    ASSERT_NO_FATAL_FAILURE(
        SendCommandAndExpectResponse(
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
    FLAGS_redis_max_concurrent_commands = FLAGS_test_redis_max_concurrent_commands;
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
  SendCommandAndExpectResponse(PipelineSetCommand(), PipelineSetResponse());
  auto mid = std::chrono::steady_clock::now();
  SendCommandAndExpectResponse(PipelineGetCommand(), PipelineGetResponse());
  auto end = std::chrono::steady_clock::now();
  auto set_time = std::chrono::duration_cast<std::chrono::milliseconds>(mid - start);
  auto get_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - mid);
  LOG(INFO) << yb::Format("Set time: $0ms, get time: $1ms", set_time.count(), get_time.count());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F_EX(TestRedisService, PipelinePartial, TestRedisServicePipelined) {
  SendCommandAndExpectResponse(PipelineSetCommand(), PipelineSetResponse(), true /* partial */);
  SendCommandAndExpectResponse(PipelineGetCommand(), PipelineGetResponse(), true /* partial */);
}

TEST_F(TestRedisService, BatchedCommandMulti) {
  SendCommandAndExpectResponse(
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
      "*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n",
      "+OK\r\n+OK\r\n+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandMultiPartial) {
  for (int i = 0; i != 1000; ++i) {
    ASSERT_NO_FATAL_FAILURE(
      SendCommandAndExpectResponse(
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
  SendCommandAndExpectTimeout("TEST");
}

TEST_F(TestRedisService, MalformedCommandsFollowedByAGoodOne) {
  ASSERT_FALSE(SendCommandAndGetResponse("*3\r\n.1\r\n", 1).ok());
  RestartClient();
  ASSERT_FALSE(SendCommandAndGetResponse("*0\r\n.2\r\n", 1).ok());
  RestartClient();
  ASSERT_FALSE(SendCommandAndGetResponse("*-4\r\n.3\r\n", 1).ok());
  RestartClient();
  SendCommandAndExpectResponse("*2\r\n$4\r\necho\r\n$3\r\nfoo\r\n", "$3\r\nfoo\r\n");
}

TEST_F(TestRedisService, IncompleteCommandMulti) {
  SendCommandAndExpectTimeout("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTE");
}

TEST_F(TestRedisService, Echo) {
  SendCommandAndExpectResponse("*2\r\n$4\r\necho\r\n$3\r\nfoo\r\n", "$3\r\nfoo\r\n");
  SendCommandAndExpectResponse("*2\r\n$4\r\necho\r\n$8\r\nfoo bar \r\n", "$8\r\nfoo bar \r\n");
  SendCommandAndExpectResponse(
      EncodeAsArrays({  // The request is sent as a multi bulk array.
                         EncodeAsBulkString("echo"),
                         EncodeAsBulkString("foo bar")
                     }),
      EncodeAsBulkString("foo bar")  // The response is in the bulk string format.
      );
}

TEST_F(TestRedisService, TestSetOnly) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$4\r\nfool\r\n$4\r\nBEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, TestCaseInsensitiveness) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nSet\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nsEt\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nseT\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, TestSetThenGet) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*2\r\n$3\r\nget\r\n$3\r\nfoo\r\n", "$4\r\nTEST\r\n");
  SendCommandAndExpectResponse(
      EncodeAsArrays({  // The request is sent as a multi bulk array.
                         EncodeAsBulkString("set"),
                         EncodeAsBulkString("name"),
                         EncodeAsBulkString("yugabyte")
                     }),
      EncodeAsSimpleString("OK")  // The response is in the simple string format.
  );
  SendCommandAndExpectResponse(
      EncodeAsArrays({  // The request is sent as a multi bulk array.
                         EncodeAsBulkString("get"),
                         EncodeAsBulkString("name")
                     }),
      EncodeAsBulkString("yugabyte")  // The response is in the bulk string format.
  );
}

TEST_F(TestRedisService, TestUsingOpenSourceClient) {

  DoRedisTest({"SET", "hello", "42"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply& reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  DoRedisTest({"DECRBY", "hello", "12"},
      cpp_redis::reply::type::error,
      [](const RedisReply &reply) {
        // TBD: ASSERT_EQ(30, reply.as_integer());
      });

  DoRedisTest({"GET", "hello"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("42", reply.as_string());
      });

  DoRedisTest({"SET", "world", "72"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();
  VerifyCallbacks();
}

TEST_F(TestRedisService, TestBinaryUsingOpenSourceClient) {

  DoRedisTest({"SET", "foo", "\001\002\r\n\003\004"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply& reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  DoRedisTest({"GET", "foo"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("\001\002\r\n\003\004", reply.as_string());
      });

  DoRedisTest({"SET", "bar", "\013\010"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply& reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  DoRedisTest({"GET", "bar"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("\013\010", reply.as_string());
      });

  SyncClient();
  VerifyCallbacks();
}

// This test also uses the open source client
TEST_F(TestRedisService, TestTtl) {

  DoRedisTest({"SET", "k1", "v1"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  DoRedisTest({"SET", "k2", "v2", "EX", "1"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  DoRedisTest({"SET", "k3", "v3", "EX", "10"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  SyncClient();
  std::this_thread::sleep_for(std::chrono::seconds(2));

  DoRedisTest({"GET", "k1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("v1", reply.as_string());
      });
  DoRedisTest({"GET", "k2"},
      cpp_redis::reply::type::null,
      [](const RedisReply &reply) {
        ASSERT_TRUE(reply.is_null());
      });
  DoRedisTest({"GET", "k3"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("v3", reply.as_string());
      });
  SyncClient();
  VerifyCallbacks();

}

TEST_F(TestRedisService, TestAdditionalCommands) {

  DoRedisTest({"HSET", "map_key", "subkey", "42"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"HGET", "map_key", "subkey"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("42", reply.as_string());
      });

  DoRedisTest({"SET", "key1", "30"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"GETSET", "key1", "val1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("30", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"GET", "key1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("val1", reply.as_string());
      });

  DoRedisTest({"APPEND", "key1", "extra1"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(10, reply.as_integer());
      });

  SyncClient();

  DoRedisTest({"GET", "key1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("val1extra1", reply.as_string());
      });

  DoRedisTest({"GET", "key2"},
      cpp_redis::reply::type::null,
      [](const RedisReply &reply) {
        ASSERT_TRUE(reply.is_null());
      });

  DoRedisTest({"SET", "key2", "val2"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"GET", "key2"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("val2", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"DEL", "key2"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(1, reply.as_integer());
      });

  SyncClient();

  DoRedisTest({"GET", "key2"},
      cpp_redis::reply::type::null,
      [](const RedisReply &reply) {
        ASSERT_TRUE(reply.is_null());
      });

  DoRedisTest({"SETRANGE", "key1", "2", "xyz3"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"GET", "key1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("vaxyz3tra1", reply.as_string());
      });

  DoRedisTest({"SET", "key3", "23"},
      cpp_redis::reply::type::simple_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("OK", reply.as_string());
      });

  SyncClient();

  DoRedisTest({"INCR", "key3"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(24, reply.as_integer());
      });

  SyncClient();

  DoRedisTest({"GET", "key3"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("24", reply.as_string());
      });

  DoRedisTest({"STRLEN", "key3"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(2, reply.as_integer());
      });

  DoRedisTest({"STRLEN", "key1"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(10, reply.as_integer());
      });

  DoRedisTest({"EXISTS", "key1"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(1, reply.as_integer());
      });

  DoRedisTest({"EXISTS", "key2"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(0, reply.as_integer());
      });

  DoRedisTest({"EXISTS", "key3"},
      cpp_redis::reply::type::integer,
      [](const RedisReply &reply) {
        ASSERT_EQ(1, reply.as_integer());
      });

  DoRedisTest(
      {"GETRANGE", "key1", "1", "-1"},
      cpp_redis::reply::type::bulk_string,
      [](const RedisReply &reply) {
        ASSERT_EQ("axyz3tra", reply.as_string());
      });

  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  SyncClient();
  VerifyCallbacks();
}

}  // namespace redisserver
}  // namespace yb
