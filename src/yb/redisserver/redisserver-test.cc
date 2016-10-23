// Copyright (c) YugaByte, Inc.

#include <memory>
#include <string>
#include <vector>

#include "cpp_redis/redis_client.hpp"
#include "cpp_redis/reply.hpp"

#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/redis_table_test_base.h"
#include "yb/redisserver/redis_server.h"
#include "yb/rpc/redis_encoding.h"
#include "yb/util/cast.h"
#include "yb/util/test_util.h"

namespace yb {
namespace redisserver {

using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::integration_tests::RedisTableTestBase;
using yb::rpc::EncodeAsArrays;
using yb::rpc::EncodeAsBulkString;
using yb::rpc::EncodeAsSimpleString;

typedef cpp_redis::redis_client RedisClient;
typedef cpp_redis::reply RedisReply;

class TestRedisService : public RedisTableTestBase {
 public:
  void SetUp() override;
  void TearDown() override;

 protected:
  void SendCommandAndExpectTimeout(const string& cmd);

  void SendCommandAndExpectResponse(const string& cmd, const string& resp);

  int server_port() { return redis_server_port_; }
 private:
  Status SendCommandAndGetResponse(
      const string& cmd, int expected_resp_length, int timeout_in_millis = 1000);

  Socket client_sock_;
  unique_ptr<RedisServer> server_;
  int redis_server_port_ = 0;
  unique_ptr<FileLock> redis_port_lock_;
  static constexpr size_t kBufLen = 1024;
  uint8_t resp_[kBufLen];
};

void TestRedisService::SetUp() {
  RedisTableTestBase::SetUp();

  redis_server_port_ = GetFreePort(&redis_port_lock_);
  RedisServerOptions opts;
  opts.rpc_opts.rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", server_port());

  auto master_rpc_addrs = master_rpc_addresses_as_strings();
  opts.master_addresses_flag = JoinStrings(master_rpc_addrs, ",");

  server_.reset(new RedisServer(opts));
  LOG(INFO) << "Initializing redis server...";
  CHECK_OK(server_->Init());

  LOG(INFO) << "Starting redis server...";
  CHECK_OK(server_->Start());
  LOG(INFO) << "Redis server successfully started.";

  Sockaddr remote;
  remote.ParseString("0.0.0.0", server_port());
  CHECK_OK(client_sock_.Init(0));
  CHECK_OK(client_sock_.SetNoDelay(false));
  LOG(INFO) << "Connecting to " << remote.ToString();
  CHECK_OK(client_sock_.Connect(remote));
}

void TestRedisService::TearDown() {
  EXPECT_OK(client_sock_.Close());
  RedisTableTestBase::TearDown();
}

Status TestRedisService::SendCommandAndGetResponse(
    const string& cmd, int expected_resp_length, int timeout_in_millis) {
  // Send the command.
  int32_t bytes_written = 0;
  EXPECT_OK(client_sock_.Write(util::to_uchar_ptr(cmd.c_str()), cmd.length(), &bytes_written));

  EXPECT_EQ(cmd.length(), bytes_written);

  // Receive the response.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(timeout_in_millis));
  size_t bytes_read = 0;
  RETURN_NOT_OK(client_sock_.BlockingRecv(resp_, expected_resp_length, &bytes_read, deadline));
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

void TestRedisService::SendCommandAndExpectResponse(const string& cmd, const string& resp) {
  CHECK_OK(SendCommandAndGetResponse(cmd, resp.length()));

  // Verify that the response is as expected.
  CHECK_EQ(resp, string(reinterpret_cast<char*>(resp_), resp.length()));
}

TEST_F(TestRedisService, SimpleCommandInline) {
  SendCommandAndExpectResponse("TEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, SimpleCommandMulti) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandsInline) {
  SendCommandAndExpectResponse("TEST1\r\nTEST2\r\nTEST3\r\nTEST4\r\n",
                               "+OK\r\n+OK\r\n+OK\r\n+OK\r\n");
}

TEST_F(TestRedisService, BatchedCommandMulti) {
  SendCommandAndExpectResponse("*3\r\n$4\r\nset1\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
                                   "*3\r\n$4\r\nset2\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n"
                                   "*3\r\n$4\r\nset3\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n",
                               "+OK\r\n+OK\r\n+OK\r\n");
}

TEST_F(TestRedisService, IncompleteCommandInline) {
  SendCommandAndExpectTimeout("TEST");
}

TEST_F(TestRedisService, IncompleteCommandMulti) {
  SendCommandAndExpectTimeout("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTE");
}

TEST_F(TestRedisService, Echo) {
  SendCommandAndExpectResponse("*2\r\n$4\r\necho\r\n$3\r\nfoo\r\n", "+foo\r\n");
  SendCommandAndExpectResponse("*2\r\n$4\r\necho\r\n$8\r\nfoo bar \r\n", "+foo bar \r\n");
  SendCommandAndExpectResponse(
      EncodeAsArrays({  // The request is sent as a multi bulk array.
                         EncodeAsBulkString("echo"),
                         EncodeAsBulkString("foo bar")
                     }),
      EncodeAsSimpleString("foo bar")  // The response is in the simple string format.
      );
}

TEST_F(TestRedisService, TestSetOnly) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$4\r\nfool\r\n$4\r\nBEST\r\n", "+OK\r\n");
}

TEST_F(TestRedisService, DISABLED_TestSetThenGet) {
  SendCommandAndExpectResponse("*3\r\n$3\r\nset\r\n$3\r\nfoo\r\n$4\r\nTEST\r\n", "+OK\r\n");
  SendCommandAndExpectResponse("*2\r\n$3\r\nget\r\n$3\r\nfoo\r\n", "+TEST\r\n");
}

TEST_F(TestRedisService, TestUsingOpenSourceClient) {
  RedisClient client;
  std::atomic_int callbacks_called(0);

  client.connect("127.0.0.1", server_port(), [] (RedisClient&) {
    LOG(ERROR) << "client disconnected (disconnection handler)" << std::endl;
  });

  // Same as client.send({ "SET", "hello", "42" }, ...)
  client.set("hello", "42", [&callbacks_called] (RedisReply& reply) {
    LOG(INFO) << "Received response : " << reply.as_string() << std::endl;
    callbacks_called++;
    ASSERT_EQ("OK", reply.as_string());
  });
  // Same as client.send({ "DECRBY", "hello", 12 }, ...)
  client.decrby("hello", 12, [&callbacks_called] (RedisReply& reply) {
    LOG(INFO) << "Received response : " << reply.as_integer() << std::endl;
    callbacks_called++;
    // TBD: ASSERT_EQ(30, reply.as_integer());
  });
  // Same as client.send({ "GET", "hello" }, ...)
  client.get("hello", [&callbacks_called] (RedisReply& reply) {
    LOG(INFO) << "Received response : " << reply.as_integer() << std::endl;
    callbacks_called++;
    // TBD: ASSERT_EQ(30, reply.as_integer());
  });
  // Same as client.send({ "SET", "world", "72" }, ...)
  client.set("world", "72", [&callbacks_called] (RedisReply& reply) {
    LOG(INFO) << "Received response : " << reply.as_string() << std::endl;
    callbacks_called++;
    ASSERT_EQ("OK", reply.as_string());
  });
  // Commands are pipelined and only sent when client.commit() is called.
  // sync_commit() waits until all responses are received.
  client.sync_commit();
  ASSERT_EQ(4, callbacks_called);
}

}  // namespace redisserver
}  // namespace yb
