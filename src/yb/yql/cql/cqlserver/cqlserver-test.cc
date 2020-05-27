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

#include <memory>
#include <string>
#include <vector>

#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/tserver/heartbeater.h"

#include "yb/yql/cql/cqlserver/cql_message.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

#include "yb/gutil/strings/join.h"
#include "yb/util/cast.h"
#include "yb/util/net/net_util.h"
#include "yb/util/test_util.h"

DECLARE_bool(cql_server_always_send_events);

namespace yb {
namespace cqlserver {

using std::string;
using std::unique_ptr;
using std::vector;
using strings::Substitute;
using yb::integration_tests::YBTableTestBase;

class TestCQLService : public YBTableTestBase {
 public:
  void SetUp() override;
  void TearDown() override;

  void TestSchemaChangeEvent();

 protected:
  void SendRequestAndExpectTimeout(const string& cmd);

  void SendRequestAndExpectResponse(const string& cmd, const string& resp);

  int server_port() { return cql_server_port_; }
 private:
  Status SendRequestAndGetResponse(
      const string& cmd, int expected_resp_length, int timeout_in_millis = 60000);

  Socket client_sock_;
  unique_ptr<boost::asio::io_service> io_;
  unique_ptr<CQLServer> server_;
  int cql_server_port_ = 0;
  unique_ptr<FileLock> cql_port_lock_;
  unique_ptr<FileLock> cql_webserver_lock_;
  static constexpr size_t kBufLen = 1024;
  uint8_t resp_[kBufLen];
};

void TestCQLService::SetUp() {
  YBTableTestBase::SetUp();

  CQLServerOptions opts;
  cql_server_port_ = GetFreePort(&cql_port_lock_);
  opts.rpc_opts.rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", cql_server_port_);
  // No need to save the webserver port, as we don't plan on using it. Just use a unique free port.
  opts.webserver_opts.port = GetFreePort(&cql_webserver_lock_);
  string fs_root = GetTestPath("CQLServerTest-fsroot");
  opts.fs_opts.wal_paths = {fs_root};
  opts.fs_opts.data_paths = {fs_root};

  auto master_rpc_addrs = master_rpc_addresses_as_strings();
  opts.master_addresses_flag = JoinStrings(master_rpc_addrs, ",");
  auto master_addresses = std::make_shared<server::MasterAddresses>();
  for (const auto& hp_str : master_rpc_addrs) {
    HostPort hp;
    CHECK_OK(hp.ParseString(hp_str, cql_server_port_));
    master_addresses->push_back({std::move(hp)});
  }
  opts.SetMasterAddresses(master_addresses);

  io_.reset(new boost::asio::io_service());
  server_.reset(new CQLServer(opts, io_.get(), mini_cluster()->mini_tablet_server(0)->server()));
  LOG(INFO) << "Starting CQL server...";
  CHECK_OK(server_->Start());
  LOG(INFO) << "CQL server successfully started.";

  Endpoint remote(IpAddress(), server_port());
  CHECK_OK(client_sock_.Init(0));
  CHECK_OK(client_sock_.SetNoDelay(false));
  LOG(INFO) << "Connecting to CQL server " << remote;
  CHECK_OK(client_sock_.Connect(remote));
}

void TestCQLService::TearDown() {
  EXPECT_OK(client_sock_.Close());
  DeleteTable();
  WARN_NOT_OK(mini_cluster()->mini_tablet_server(0)->server()->heartbeater()->Stop(),
              "Failed to stop heartbeater");
  server_->Shutdown();
  YBTableTestBase::TearDown();
}

Status TestCQLService::SendRequestAndGetResponse(
    const string& cmd, int expected_resp_length, int timeout_in_millis) {
  // Send the request.
  int32_t bytes_written = 0;
  EXPECT_OK(client_sock_.Write(util::to_uchar_ptr(cmd.c_str()), cmd.length(), &bytes_written));

  EXPECT_EQ(cmd.length(), bytes_written);

  // Receive the response.
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(MonoDelta::FromMilliseconds(timeout_in_millis));
  size_t bytes_read = 0;
  RETURN_NOT_OK(client_sock_.BlockingRecv(resp_, expected_resp_length, &bytes_read, deadline));
  if (expected_resp_length != bytes_read) {
    return STATUS(
        IOError, Substitute("Received $1 bytes instead of $2", bytes_read, expected_resp_length));
  }

  // Try to read 1 more byte - the read must fail (no more data in the socket).
  bytes_read = 0;
  deadline = MonoTime::Now();
  deadline.AddDelta(MonoDelta::FromMilliseconds(200));
  Status s = client_sock_.BlockingRecv(&resp_[expected_resp_length], 1, &bytes_read, deadline);
  EXPECT_EQ(0, bytes_read) << "In the read socket unexpected extra byte: 0x" << std::hex <<
      static_cast<int>(resp_[expected_resp_length]) <<
      " (0x84 usually means additional unexpected CQL message)";
  EXPECT_TRUE(s.IsTimedOut());

  return Status::OK();
}

void TestCQLService::SendRequestAndExpectTimeout(const string& cmd) {
  // Don't expect to receive even 1 byte.
  ASSERT_TRUE(SendRequestAndGetResponse(cmd, 1).IsTimedOut());
}

void TestCQLService::SendRequestAndExpectResponse(const string& cmd, const string& resp) {
  CHECK_OK(SendRequestAndGetResponse(cmd, resp.length()));

  // Verify that the response is as expected.
  CHECK_EQ(resp, string(reinterpret_cast<char*>(resp_), resp.length()));
}

// The following test cases test the CQL protocol marshalling/unmarshalling with hand-coded
// request messages and expected responses. They are good as basic and error-handling tests.
// These are expected to be few.
//
// TODO(Robert) - add more tests using Cassandra C++ driver when the CQL server can respond
// to queries.
TEST_F(TestCQLService, StartupRequest) {
  LOG(INFO) << "Test CQL STARTUP request";
  // Send STARTUP request using version V3
  SendRequestAndExpectResponse(
      BINARY_STRING("\x03\x00\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"),
      BINARY_STRING("\x83\x00\x00\x00\x02" "\x00\x00\x00\x00"));

  // Send STARTUP request using version V4
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"),
      BINARY_STRING("\x84\x00\x00\x00\x02" "\x00\x00\x00\x00"));

  // Send STARTUP request using version V5
  SendRequestAndExpectResponse(
      BINARY_STRING("\x05\x00\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x58"
                    "\x00\x00\x00\x0a" "\x00\x52"
                    "Invalid or unsupported protocol version 5. "
                    "Supported versions are between 3 and 4."));

  // Send STARTUP request with compression
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x01\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x2e"
                    "\x00\x00\x00\x0a" "\x00\x28"
                    "STARTUP request should not be compressed"));
}

TEST_F(TestCQLService, OptionsRequest) {
  LOG(INFO) << "Test CQL OPTIONS request";
  // Send OPTIONS request using version V4
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x05" "\x00\x00\x00\x00"),
      BINARY_STRING("\x84\x00\x00\x00\x06" "\x00\x00\x00\x3b"
                    "\x00\x02" "\x00\x0b" "COMPRESSION"
                               "\x00\x02" "\x00\x03" "lz4" "\x00\x06" "snappy"
                               "\x00\x0b" "CQL_VERSION"
                               "\x00\x02" "\x00\x05" "3.0.0" "\x00\x05" "3.4.2"));
}

TEST_F(TestCQLService, InvalidRequest) {
  LOG(INFO) << "Test invalid CQL request";
  // Send response (0x84) as request
  SendRequestAndExpectResponse(
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x22"
                    "\x00\x00\x00\x0a" "\x00\x1c" "Unsupported protocol version"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x13"
                    "\x00\x00\x00\x0a" "\x00\x0d" "Not a request"));

  // Send ERROR as request
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x00" "\x00\x00\x00\x22"
                    "\x00\x00\x00\x0a" "\x00\x1c" "Unsupported protocol version"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x1a"
                    "\x00\x00\x00\x0a" "\x00\x14" "Not a request opcode"));

  // Send an unknown opcode
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\xff" "\x00\x00\x00\x22"
                    "\x00\x00\x00\x0a" "\x00\x1c" "Unsupported protocol version"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x14"
                    "\x00\x00\x00\x0a" "\x00\x0e" "Unknown opcode"));

  // Send truncated request
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x01" "\x00\x00\x00\x0f"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x1b"
                    "\x00\x00\x00\x0a" "\x00\x15" "Truncated CQL message"));

  // Send truncated string in request
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x15" "3.0.0"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x1b"
                    "\x00\x00\x00\x0a" "\x00\x15" "Truncated CQL message"));

  // Send request with extra trailing bytes
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x01" "\x00\x00\x00\x18"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"
                               "\x00\0x00"),
      BINARY_STRING("\x84\x00\x00\x00\x00" "\x00\x00\x00\x1d"
                    "\x00\x00\x00\x0a" "\x00\x17" "Request length too long"));
}

TEST_F(TestCQLService, TestCQLServerEventConst) {
  std::unique_ptr<SchemaChangeEventResponse> response(
      new SchemaChangeEventResponse("", "", "", "", {}));
  constexpr size_t kSize = sizeof(CQLServerEvent);
  std::unique_ptr<CQLServerEvent> event(new CQLServerEvent(std::move(response)));
  auto event_list = std::make_shared<CQLServerEventList>();
  event_list->AddEvent(std::move(event));
  yb::rpc::OutboundDataPtr data(event_list);
  void* ptr = event_list.get();
  char buffer[kSize];
  memcpy(buffer, ptr, kSize);
  data->Transferred(Status::OK(), nullptr);
  ASSERT_EQ(0, memcmp(buffer, ptr, kSize));
  data->Transferred(STATUS(NetworkError, "Dummy"), nullptr);
  ASSERT_EQ(0, memcmp(buffer, ptr, kSize));
}

void TestCQLService::TestSchemaChangeEvent() {
  LOG(INFO) << "Test CQL SCHEMA_CHANGE event with gflag cql_server_always_send_events = " <<
      FLAGS_cql_server_always_send_events;

  // Send STARTUP request using version V4.
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x01" "\x00\x00\x00\x16"
                    "\x00\x01" "\x00\x0b" "CQL_VERSION"
                               "\x00\x05" "3.0.0"),
      BINARY_STRING("\x84\x00\x00\x00\x02" "\x00\x00\x00\x00"));

  // Send CREATE KEYSPACE IF NOT EXISTS "kong"
  //      WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor': 1}
  // Expecting only one CQL message as the result: CQL RESULT (opcode=8).
  string expected_response =
      BINARY_STRING("\x84\x00\x00\x00\x08" "\x00\x00\x00\x1d" "\x00\x00\x00\x05"
                                "\x00\x07" "CREATED"
                                "\x00\x08" "KEYSPACE"
                                "\x00\x04" "kong");
  if (FLAGS_cql_server_always_send_events) {
    // Expecting 2 CQL messages as the result: CQL RESULT (opcode=8) + CQL EVENT (opcode=0x0c).
    expected_response +=
        BINARY_STRING("\x84\x00\xff\xff\x0c" "\x00\x00\x00\x28"
                                  "\x00\x0d" "SCHEMA_CHANGE"
                                  "\x00\x07" "CREATED"
                                  "\x00\x08" "KEYSPACE"
                                  "\x00\x04" "kong");
  }

  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x07" "\x00\x00\x00\x99"
                    "\x00\x00"  "\x00\x8c" "      CREATE KEYSPACE IF NOT EXISTS \"kong\""
                                    "\x0a" "      WITH REPLICATION =         {'class': "
                                           "'SimpleStrategy', 'replication_factor': 1}"
                                    "\x0a" "      "
                                    "\x0a" "    "
                                "\x00\x01" "\x14\x00\x00\x03\xe8\x00\x08"),
      expected_response);

  // Send USE "kong"
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x07" "\x00\x00\x00\x11"
                    "\x00\x00"  "\x00\x0a" "USE \"kong\""
                    "\x00\x01\x00"),
      BINARY_STRING("\x84\x00\x00\x00\x08" "\x00\x00\x00\x0a" "\x00\x00\x00\x03"
                                "\x00\x04" "kong"));

  // Send CREATE TABLE IF NOT EXISTS schema_meta(
  //          key text, subsystem text, last_executed text, executed set<text>,
  //          pending set<text>, PRIMARY KEY (key, subsystem))
  // Expecting only one CQL message as the result: CQL RESULT (opcode=8).
  expected_response =
      BINARY_STRING("\x84\x00\x00\x00\x08" "\x00\x00\x00\x27" "\x00\x00\x00\x05"
                                "\x00\x07" "CREATED"
                                "\x00\x05" "TABLE"
                                "\x00\x04" "kong"
                                "\x00\x0b" "schema_meta");
  if (FLAGS_cql_server_always_send_events) {
    // Expecting 2 CQL messages as the result: CQL RESULT (opcode=8) + CQL EVENT (opcode=0x0c).
    expected_response +=
      BINARY_STRING("\x84\x00\xff\xff\x0c" "\x00\x00\x00\x32"
                                "\x00\x0d" "SCHEMA_CHANGE"
                                "\x00\x07" "CREATED"
                                "\x00\x05" "TABLE"
                                "\x00\x04" "kong"
                                "\x00\x0b" "schema_meta");
  }

  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x07" "\x00\x00\x01\x0d"
                    "\x00\x00"  "\x01\x00" "      CREATE TABLE IF NOT EXISTS schema_meta("
                                    "\x0a" "        key             text,"
                                    "\x0a" "        subsystem       text,"
                                    "\x0a" "        last_executed   text,"
                                    "\x0a" "        executed        set<text>,"
                                    "\x0a" "        pending         set<text>,"
                                    "\x0a"
                                    "\x0a" "        PRIMARY KEY (key, subsystem)"
                                    "\x0a" "      )"
                                    "\x0a" "    "
                                "\x00\x01" "\x14\x00\x00\x03\xe8\x00\x08"),
      expected_response);

  // Send REGISTER request to subscribe for the events: TOPOLOGY_CHANGE, STATUS_CHANGE,
  //                                                    SCHEMA_CHANGE
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x0b" "\x00\x00\x00\x31"
                    "\x00\x03"  "\x00\x0f" "TOPOLOGY_CHANGE"
                                "\x00\x0d" "STATUS_CHANGE"
                                "\x00\x0d" "SCHEMA_CHANGE"),
      BINARY_STRING("\x84\x00\x00\x00\x02" "\x00\x00\x00\x00"));

  // Send CREATE TABLE IF NOT EXISTS schema_meta2(
  //          key text, subsystem text, last_executed text, executed set<text>,
  //          pending set<text>, PRIMARY KEY (key, subsystem))
  SendRequestAndExpectResponse(
      BINARY_STRING("\x04\x00\x00\x00\x07" "\x00\x00\x01\x0d"
                    "\x00\x00"  "\x01\x00" "      CREATE TABLE IF NOT EXISTS schema_meta2("
                                    "\x0a" "        key             text,"
                                    "\x0a" "        subsystem       text,"
                                    "\x0a" "        last_executed   text,"
                                    "\x0a" "        executed        set<text>,"
                                    "\x0a" "        pending         set<text>,"
                                    "\x0a"
                                    "\x0a" "        PRIMARY KEY (key, subsystem)"
                                    "\x0a" "      )"
                                    "\x0a" "   "
                                "\x00\x01" "\x14\x00\x00\x03\xe8\x00\x08"),
      // Expecting 2 CQL messages as the result: CQL RESULT (opcode=8) + CQL EVENT (opcode=0x0c).
      BINARY_STRING("\x84\x00\x00\x00\x08" "\x00\x00\x00\x28" "\x00\x00\x00\x05"
                                "\x00\x07" "CREATED"
                                "\x00\x05" "TABLE"
                                "\x00\x04" "kong"
                                "\x00\x0c" "schema_meta2"
                    "\x84\x00\xff\xff\x0c" "\x00\x00\x00\x33"
                                "\x00\x0d" "SCHEMA_CHANGE"
                                "\x00\x07" "CREATED"
                                "\x00\x05" "TABLE"
                                "\x00\x04" "kong"
                                "\x00\x0c" "schema_meta2"));
}

TEST_F(TestCQLService, SchemaChangeEvent) {
  TestSchemaChangeEvent();
}

class TestCQLServiceWithGFlag : public TestCQLService {
 public:
  void SetUp() override {
    FLAGS_cql_server_always_send_events = true;
    TestCQLService::SetUp();
  }
};

TEST_F(TestCQLServiceWithGFlag, SchemaChangeEventWithGFlag) {
  TestSchemaChangeEvent();
}

}  // namespace cqlserver
}  // namespace yb
