// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/tserver/tablet_server-test-base.h"

#include "yb/consensus/log-test-base.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.pb.h"
#include "yb/server/hybrid_clock.h"
#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"
#include "yb/util/crc.h"
#include "yb/util/curl_util.h"
#include "yb/util/url-coding.h"

using yb::consensus::RaftConfigPB;
using yb::consensus::RaftPeerPB;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using yb::server::Clock;
using yb::server::HybridClock;
using yb::tablet::Tablet;
using yb::tablet::TabletPeer;
using std::shared_ptr;
using std::string;
using strings::Substitute;

DEFINE_int32(single_threaded_insert_latency_bench_warmup_rows, 100,
             "Number of rows to insert in the warmup phase of the single threaded"
             " tablet server insert latency micro-benchmark");

DEFINE_int32(single_threaded_insert_latency_bench_insert_rows, 1000,
             "Number of rows to insert in the testing phase of the single threaded"
             " tablet server insert latency micro-benchmark");

DECLARE_int32(scanner_batch_size_rows);
DECLARE_int32(metrics_retirement_age_ms);
DECLARE_string(block_manager);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(disable_clock_sync_error);

// Declare these metrics prototypes for simpler unit testing of their behavior.
METRIC_DECLARE_counter(rows_inserted);
METRIC_DECLARE_counter(rows_updated);
METRIC_DECLARE_counter(rows_deleted);

namespace yb {
namespace tserver {

class TabletServerTest : public TabletServerTestBase {
 public:
  explicit TabletServerTest(TableType table_type = YQL_TABLE_TYPE)
      : TabletServerTestBase(table_type) {}

  // Starts the tablet server, override to start it later.
  void SetUp() override {
    TabletServerTestBase::SetUp();
    StartTabletServer();
  }

  void DoOrderedScanTest(const Schema& projection, const string& expected_rows_as_string);
};

TEST_F(TabletServerTest, TestPingServer) {
  // Ping the server.
  server::PingRequestPB req;
  server::PingResponsePB resp;
  RpcController controller;
  ASSERT_OK(generic_proxy_->Ping(req, &resp, &controller));
}

TEST_F(TabletServerTest, TestServerClock) {
  server::ServerClockRequestPB req;
  server::ServerClockResponsePB resp;
  RpcController controller;

  ASSERT_OK(generic_proxy_->ServerClock(req, &resp, &controller));
  ASSERT_GT(mini_server_->server()->clock()->Now().ToUint64(), resp.hybrid_time());
}

TEST_F(TabletServerTest, TestSetFlagsAndCheckWebPages) {
  server::GenericServiceProxy proxy(
      client_messenger_, mini_server_->bound_rpc_addr());

  server::SetFlagRequestPB req;
  server::SetFlagResponsePB resp;

  // Set an invalid flag.
  {
    RpcController controller;
    req.set_flag("foo");
    req.set_value("bar");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    EXPECT_EQ(server::SetFlagResponsePB::NO_SUCH_FLAG, resp.result());
    EXPECT_TRUE(resp.msg().empty());
  }

  // Set a valid flag to a valid value.
  {
    int32_t old_val = FLAGS_metrics_retirement_age_ms;
    RpcController controller;
    req.set_flag("metrics_retirement_age_ms");
    req.set_value("12345");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    EXPECT_EQ(server::SetFlagResponsePB::SUCCESS, resp.result());
    EXPECT_EQ(resp.msg(), "metrics_retirement_age_ms set to 12345\n");
    EXPECT_EQ(Substitute("$0", old_val), resp.old_value());
    EXPECT_EQ(12345, FLAGS_metrics_retirement_age_ms);
  }

  // Set a valid flag to an invalid value.
  {
    RpcController controller;
    req.set_flag("metrics_retirement_age_ms");
    req.set_value("foo");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    EXPECT_EQ(server::SetFlagResponsePB::BAD_VALUE, resp.result());
    EXPECT_EQ(resp.msg(), "Unable to set flag: bad value");
    EXPECT_EQ(12345, FLAGS_metrics_retirement_age_ms);
  }

  // Try setting a flag which isn't runtime-modifiable
  {
    RpcController controller;
    req.set_flag("tablet_do_dup_key_checks");
    req.set_value("true");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    EXPECT_EQ(server::SetFlagResponsePB::NOT_SAFE, resp.result());
  }

  // Try again, but with the force flag.
  {
    RpcController controller;
    req.set_flag("tablet_do_dup_key_checks");
    req.set_value("true");
    req.set_force(true);
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    EXPECT_EQ(server::SetFlagResponsePB::SUCCESS, resp.result());
  }

  EasyCurl c;
  faststring buf;
  string addr = yb::ToString(mini_server_->bound_http_addr());

  // Tablets page should list tablet.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tablets", addr),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), kTabletId);
  ASSERT_STR_CONTAINS(buf.ToString(), "<td>range: [(&lt;start&gt;), (&lt;end&gt;))</td>");

  // Tablet page should include the schema.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tablet?id=$1", addr, kTabletId),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "<th>key</th>");
  ASSERT_STR_CONTAINS(buf.ToString(), "<td>string NULLABLE</td>");

  // Test fetching metrics.
  // Fetching metrics has the side effect of retiring metrics, but not in a single pass.
  // So, we check a couple of times in a loop -- thus, if we had a bug where one of these
  // metrics was accidentally un-referenced too early, we'd cause it to get retired.
  // If the metrics survive several passes of fetching, then we are pretty sure they will
  // stick around properly for the whole lifetime of the server.
  FLAGS_metrics_retirement_age_ms = 0;
  for (int i = 0; i < 3; i++) {
    SCOPED_TRACE(i);
    ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/jsonmetricz", addr, kTabletId),
                                &buf));

    // Check that the tablet entry shows up.
    ASSERT_STR_CONTAINS(buf.ToString(), "\"type\": \"tablet\"");
    ASSERT_STR_CONTAINS(buf.ToString(), "\"id\": \"test-tablet\"");
    ASSERT_STR_CONTAINS(buf.ToString(), "\"partition\": \"range: [(<start>), (<end>))\"");

    // Check entity attributes.
    ASSERT_STR_CONTAINS(buf.ToString(), "\"table_name\": \"test-table\"");

    // Check for the existence of some particular metrics for which we've had early-retirement
    // bugs in the past.
    ASSERT_STR_CONTAINS(buf.ToString(), "hybrid_clock_hybrid_time");
    ASSERT_STR_CONTAINS(buf.ToString(), "active_scanners");
    ASSERT_STR_CONTAINS(buf.ToString(), "threads_started");
#ifdef TCMALLOC_ENABLED
    ASSERT_STR_CONTAINS(buf.ToString(), "tcmalloc_max_total_thread_cache_bytes");
#endif
    ASSERT_STR_CONTAINS(buf.ToString(), "glog_info_messages");
  }

  // Smoke-test the tracing infrastructure.
  ASSERT_OK(c.FetchURL(
                Substitute("http://$0/tracing/json/get_buffer_percent_full", addr, kTabletId),
                &buf));
  ASSERT_EQ(buf.ToString(), "0");

  string enable_req_json = "{\"categoryFilter\":\"*\", \"useContinuousTracing\": \"true\","
    " \"useSampling\": \"false\"}";
  string req_b64;
  Base64Escape(enable_req_json, &req_b64);

  ASSERT_OK(c.FetchURL(Substitute("http://$0/tracing/json/begin_recording?$1",
                                         addr,
                                         req_b64), &buf));
  ASSERT_EQ(buf.ToString(), "");
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tracing/json/end_recording", addr),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "__metadata");
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tracing/json/categories", addr),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "\"rpc\"");

  // Smoke test the pprof contention profiler handler.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/pprof/contention?seconds=1", addr),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "Discarded samples = 0");
#if defined(__linux__)
  // The executable name appears as part of the dump of /proc/self/maps, which
  // only exists on Linux.
  ASSERT_STR_CONTAINS(buf.ToString(), "tablet_server-test");
#endif
}

TEST_F(TabletServerTest, TestInsert) {
  WriteRequestPB req;

  req.set_tablet_id(kTabletId);

  WriteResponsePB resp;
  RpcController controller;

  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));
  scoped_refptr<Counter> rows_inserted =
    METRIC_rows_inserted.Instantiate(tablet->tablet()->GetMetricEntity());
  ASSERT_EQ(0, rows_inserted->value());
  tablet.reset();

  // Send a bad insert which has an empty schema. This should result
  // in an error.
  {
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1234, 5678, "hello world via RPC",
                   req.mutable_row_operations());

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::UNKNOWN_ERROR, resp.error().code());
    Status s = StatusFromPB(resp.error().status());
    EXPECT_TRUE(s.IsInvalidArgument());
    ASSERT_STR_CONTAINS(s.ToString(),
                        "Client missing required column: key[int32 NOT NULL]");
    req.clear_row_operations();
  }

  // Send an empty request with the correct schema.
  // This should succeed and do nothing.
  {
    controller.Reset();
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    req.clear_row_operations();
  }

  // Send an actual row insert.
  {
    controller.Reset();
    RowOperationsPB* data = req.mutable_row_operations();
    data->Clear();

    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1234, 5678,
                   "hello world via RPC", data);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    req.clear_row_operations();
  }

  // Send a batch with multiple rows, one of which is a duplicate of
  // the above insert. This should generate one error into per_row_errors.
  {
    controller.Reset();
    RowOperationsPB* data = req.mutable_row_operations();
    data->Clear();

    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1, 1, "ceci n'est pas une dupe", data);
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 2, 1, "also not a dupe key", data);
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1234, 1, "I am a duplicate key", data);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }

  // get the clock's current hybrid_time
  HybridTime now_before = mini_server_->server()->clock()->Now();

  rows_inserted = nullptr;
  ASSERT_OK(ShutdownAndRebuildTablet());
  VerifyRows(schema_, { KeyValue(1, 1), KeyValue(2, 1), KeyValue(1234, 5678) });

  // get the clock's hybrid_time after replay
  HybridTime now_after = mini_server_->server()->clock()->Now();

  // make sure 'now_after' is greater than or equal to 'now_before'
  ASSERT_GE(now_after.value(), now_before.value());
}

TEST_F(TabletServerTest, TestExternalConsistencyModes_ClientPropagated) {
  WriteRequestPB req;
  req.set_tablet_id(kTabletId);
  WriteResponsePB resp;
  RpcController controller;

  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(
      mini_server_->server()->tablet_manager()->LookupTablet(kTabletId,
                                                             &tablet));
  // get the current time
  HybridTime current = mini_server_->server()->clock()->Now();
  // advance current to some time in the future. we do 5 secs to make
  // sure this hybrid_time will still be in the future when it reaches the
  // server.
  current = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(current) + 5000000);

  // Send an actual row insert.
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1234, 5678, "hello world via RPC",
                 req.mutable_row_operations());

  // set the external consistency mode and the hybrid_time
  req.set_external_consistency_mode(CLIENT_PROPAGATED);

  req.set_propagated_hybrid_time(current.ToUint64());
  SCOPED_TRACE(req.DebugString());
  ASSERT_OK(proxy_->Write(req, &resp, &controller));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error());
  req.clear_row_operations();

  // make sure the server returned a write hybrid_time where only
  // the logical value was increased since he should have updated
  // its clock with the client's value.
  HybridTime write_hybrid_time(resp.hybrid_time());

  ASSERT_EQ(HybridClock::GetPhysicalValueMicros(current),
            HybridClock::GetPhysicalValueMicros(write_hybrid_time));

  ASSERT_EQ(HybridClock::GetLogicalValue(current) + 1,
            HybridClock::GetLogicalValue(write_hybrid_time));
}

TEST_F(TabletServerTest, TestExternalConsistencyModes_CommitWait) {
  WriteRequestPB req;
  req.set_tablet_id(kTabletId);
  WriteResponsePB resp;
  RpcController controller;
  HybridClock* hclock = down_cast<HybridClock*, Clock>(mini_server_->server()->clock());

  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(
      mini_server_->server()->tablet_manager()->LookupTablet(kTabletId,
                                                             &tablet));
  // get current time, with and without error
  HybridTime now_before;
  uint64_t error_before;
  hclock->NowWithError(&now_before, &error_before);

  uint64_t now_before_usec = HybridClock::GetPhysicalValueMicros(now_before);
  LOG(INFO) << "Submitting write with commit wait at: " << now_before_usec << " us +- "
      << error_before << " us";

  // Send an actual row insert.
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));
  AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1234, 5678, "hello world via RPC",
                 req.mutable_row_operations());

  // set the external consistency mode to COMMIT_WAIT
  req.set_external_consistency_mode(COMMIT_WAIT);

  SCOPED_TRACE(req.DebugString());
  ASSERT_OK(proxy_->Write(req, &resp, &controller));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error());
  req.clear_row_operations();

  // Two things must have happened.
  // 1 - The write hybrid_time must be greater than 'now_before'
  // 2 - The write must have taken at least 'error_before' to complete (two
  //     times more in average).

  HybridTime now_after;
  uint64_t error_after;
  hclock->NowWithError(&now_after, &error_after);

  HybridTime write_hybrid_time(resp.hybrid_time());

  uint64_t write_took = HybridClock::GetPhysicalValueMicros(now_after) -
      HybridClock::GetPhysicalValueMicros(now_before);

  LOG(INFO) << "Write applied at: " << HybridClock::GetPhysicalValueMicros(write_hybrid_time)
      << " us, current time: " << HybridClock::GetPhysicalValueMicros(now_after)
      << " us, write took: " << write_took << " us";

  ASSERT_GT(write_hybrid_time.value(), now_before.value());

  // see HybridClockTest.TestWaitUntilAfter_TestCase2
  if (error_after >= error_before) {
    ASSERT_GE(write_took, 2 * error_before);
  } else {
    ASSERT_GE(write_took, error_before);
  }
}

TEST_F(TabletServerTest, TestInsertAndMutate) {

  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));
  tablet.reset();

  RpcController controller;

  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    RowOperationsPB* data = req.mutable_row_operations();
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 1, 1, "original1", data);
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 2, 2, "original2", data);
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 3, 3, "original3", data);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // Try and mutate the rows inserted above
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 1, 2, "mutation1",
                   req.mutable_row_operations());
    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 2, 3, "mutation2",
                   req.mutable_row_operations());
    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 3, 4, "mutation3",
                   req.mutable_row_operations());
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // Try and mutate a non existent row key (should not get an error)
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 1234, 2, "mutated",
                   req.mutable_row_operations());
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // Try and delete 1 row
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    AddTestKeyToPB(RowOperationsPB::DELETE, schema_, 1, req.mutable_row_operations());
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // Now try and mutate a row we just deleted, we should not get an error
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 1, 2, "mutated1",
                   req.mutable_row_operations());
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // At this point, we have two left.
  VerifyRows(schema_, {KeyValue(1, 2), KeyValue(2, 3), KeyValue(3, 4), KeyValue(1234, 2)});

  // Do a mixed operation (some insert, update, and delete, some of which fail)
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

    RowOperationsPB* ops = req.mutable_row_operations();
    // op 0: Mutate row 1, which doesn't exist. This should not fail.
    AddTestRowToPB(RowOperationsPB::UPDATE, schema_, 1, 3, "mutate_should_not_fail", ops);
    // op 1: Insert a new row 4 (succeeds)
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 4, 4, "new row 4", ops);
    // op 2: Delete a non-existent row 5 (should fail)
    AddTestKeyToPB(RowOperationsPB::DELETE, schema_, 5, ops);
    // op 3: Insert a new row 6 (succeeds)
    AddTestRowToPB(RowOperationsPB::INSERT, schema_, 6, 6, "new row 6", ops);

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // get the clock's current hybrid_time
  HybridTime now_before = mini_server_->server()->clock()->Now();

  ASSERT_NO_FATALS(WARN_NOT_OK(ShutdownAndRebuildTablet(), "Shutdown failed: "));
  VerifyRows(schema_,
      { KeyValue(1, 3), KeyValue(2, 3), KeyValue(3, 4), KeyValue(4, 4), KeyValue(6, 6),
        KeyValue(1234, 2) });

  // get the clock's hybrid_time after replay
  HybridTime now_after = mini_server_->server()->clock()->Now();

  // make sure 'now_after' is greater that or equal to 'now_before'
  ASSERT_GE(now_after.value(), now_before.value());
}

// Test that passing a schema with fields not present in the tablet schema
// throws an exception.
TEST_F(TabletServerTest, TestInvalidWriteRequest_BadSchema) {
  SchemaBuilder schema_builder(schema_);
  ASSERT_OK(schema_builder.AddColumn("col_doesnt_exist", INT32));
  Schema bad_schema_with_ids = schema_builder.Build();
  Schema bad_schema = schema_builder.BuildWithoutIds();

  // Send a row insert with an extra column
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    RpcController controller;

    req.set_tablet_id(kTabletId);
    RowOperationsPB* data = req.mutable_row_operations();
    ASSERT_OK(SchemaToPB(bad_schema, req.mutable_schema()));

    YBPartialRow row(&bad_schema);
    CHECK_OK(row.SetInt32("key", 1234));
    CHECK_OK(row.SetInt32("int_val", 5678));
    CHECK_OK(row.SetStringCopy("string_val", "hello world via RPC"));
    CHECK_OK(row.SetInt32("col_doesnt_exist", 91011));
    RowOperationsPBEncoder enc(data);
    enc.Add(RowOperationsPB::INSERT, row);

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::UNKNOWN_ERROR, resp.error().code());
    ASSERT_STR_CONTAINS(resp.error().status().message(),
                        "Client provided column col_doesnt_exist[int32 NOT NULL]"
                        " not present in tablet");
  }

  // Send a row mutation with an extra column and IDs
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    RpcController controller;

    req.set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToPB(bad_schema_with_ids, req.mutable_schema()));

    AddTestKeyToPB(RowOperationsPB::UPDATE, bad_schema_with_ids, 1,
                   req.mutable_row_operations());
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::UNKNOWN_ERROR, resp.error().code());
    ASSERT_STR_CONTAINS(resp.error().status().message(),
                        "User requests should not have Column IDs");
  }
}

// Executes mutations each time a Tablet goes through a compaction/flush
// lifecycle hook. This allows to create mutations of all possible types
// deterministically. The purpose is to make sure such mutations are replayed
// correctly on tablet bootstrap.
class MyCommonHooks : public Tablet::FlushCompactCommonHooks,
                      public Tablet::FlushFaultHooks,
                      public Tablet::CompactionFaultHooks {
 public:
  explicit MyCommonHooks(TabletServerTest* test)
  : test_(test),
    iteration_(0) {}

  Status DoHook(int32_t key, int32_t new_int_val) {
    test_->UpdateTestRowRemote(0, key, new_int_val);
    return Status::OK();
  }

  // This should go in pre-flush and get flushed
  Status PostSwapNewMemRowSet() override {
    return DoHook(1, 10 + iteration_);
  }
  // This should go in after the flush, but before
  // the duplicating row set, i.e., this should appear as
  // a missed delta.
  Status PostTakeMvccSnapshot() override {
    return DoHook(2, 20 + iteration_);
  }
  // This too should appear as a missed delta.
  Status PostWriteSnapshot() override {
    return DoHook(3, 30 + iteration_);
  }
  // This should appear as a duplicated mutation
  Status PostSwapInDuplicatingRowSet() override {
    return DoHook(4, 40 + iteration_);
  }
  // This too should appear as a duplicated mutation
  Status PostReupdateMissedDeltas() override {
    return DoHook(5, 50 + iteration_);
  }
  // This should go into the new delta.
  Status PostSwapNewRowSet() override {
    return DoHook(6, 60 + iteration_);
  }
  // This should go in pre-flush (only on compactions)
  Status PostSelectIterators() override {
    return DoHook(7, 70 + iteration_);
  }
  void increment_iteration() {
    iteration_++;
  }
 protected:
  TabletServerTest* test_;
  int iteration_;
};

// Regression test for KUDU-176. Ensures that after a major delta compaction,
// restarting properly recovers the tablet.
TEST_F(TabletServerTest, TestKUDU_176_RecoveryAfterMajorDeltaCompaction) {
  // Flush a DRS with 1 rows.
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 1));
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 1) }));

  // Update it, flush deltas.
  ASSERT_NO_FATALS(UpdateTestRowRemote(0, 1, 2));
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 2) }));

  // Verify that data is still the same.
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 2) }));

  // Verify that data remains after a restart.
  ASSERT_OK(ShutdownAndRebuildTablet());
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 2) }));
}

// Regression test for KUDU-177. Ensures that after a major delta compaction,
// rows that were in the old DRS's DMS are properly replayed.
TEST_F(TabletServerTest, TestKUDU_177_RecoveryOfDMSEditsAfterMajorDeltaCompaction) {
  // Flush a DRS with 1 rows.
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 1));
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 1) }));

  // Update it, flush deltas.
  ASSERT_NO_FATALS(UpdateTestRowRemote(0, 1, 2));
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());

  // Update it again, so this last update is in the DMS.
  ASSERT_NO_FATALS(UpdateTestRowRemote(0, 1, 3));
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 3) }));

  // Verify that data is still the same.
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 3) }));

  // Verify that the update remains after a restart.
  ASSERT_OK(ShutdownAndRebuildTablet());
  ASSERT_NO_FATALS(VerifyRows(schema_, { KeyValue(1, 3) }));
}

TEST_F(TabletServerTest, TestClientGetsErrorBackWhenRecoveryFailed) {
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 7));

  ASSERT_OK(tablet_peer_->tablet()->Flush());

  // Save the log path before shutting down the tablet (and destroying
  // the tablet peer).
  string log_path = tablet_peer_->log()->ActiveSegmentPathForTests();
  ShutdownTablet();

  ASSERT_OK(log::CorruptLogFile(env_.get(), log_path, log::FLIP_BYTE, 300));

  ASSERT_FALSE(ShutdownAndRebuildTablet().ok());

  // Connect to it.
  CreateTsClientProxies(mini_server_->bound_rpc_addr(),
                        client_messenger_,
                        &proxy_, &admin_proxy_, &consensus_proxy_, &generic_proxy_);

  WriteRequestPB req;
  req.set_tablet_id(kTabletId);

  WriteResponsePB resp;
  rpc::RpcController controller;

  // We're expecting the write to fail.
  ASSERT_OK(DCHECK_NOTNULL(proxy_.get())->Write(req, &resp, &controller));
  ASSERT_EQ(TabletServerErrorPB::TABLET_NOT_RUNNING, resp.error().code());
  ASSERT_STR_CONTAINS(resp.error().status().message(), "Tablet not RUNNING: FAILED");
}

TEST_F(TabletServerTest, TestScan) {
  int num_rows = AllowSlowTests() ? 10000 : 1000;
  InsertTestRowsDirect(0, num_rows);

  ScanResponsePB resp;
  ASSERT_NO_FATALS(OpenScannerWithAllColumns(&resp));

  // Ensure that the scanner ID came back and got inserted into the
  // ScannerManager map.
  string scanner_id = resp.scanner_id();
  ASSERT_TRUE(!scanner_id.empty());
  {
    SharedScanner junk;
    ASSERT_TRUE(mini_server_->server()->scanner_manager()->LookupScanner(scanner_id, &junk));
  }

  // Drain all the rows from the scanner.
  vector<string> results;
  ASSERT_NO_FATALS(
    DrainScannerToStrings(resp.scanner_id(), schema_, &results));
  ASSERT_EQ(num_rows, results.size());

  YBPartialRow row(&schema_);
  for (int i = 0; i < num_rows; i++) {
    BuildTestRow(i, &row);
    string expected = "(" + row.ToString() + ")";
    ASSERT_EQ(expected, results[i]);
  }

  // Since the rows are drained, the scanner should be automatically removed
  // from the scanner manager.
  {
    SharedScanner junk;
    ASSERT_FALSE(mini_server_->server()->scanner_manager()->LookupScanner(scanner_id, &junk));
  }
}

TEST_F(TabletServerTest, TestScannerOpenWhenServerShutsDown) {
  InsertTestRowsDirect(0, 1);

  ScanResponsePB resp;
  ASSERT_NO_FATALS(OpenScannerWithAllColumns(&resp));

  // Scanner is now open. The test will now shut down the TS with the scanner still
  // out there. Due to KUDU-161 this used to fail, since the scanner (and thus the MRS)
  // stayed open longer than the anchor registry
}

TEST_F(TabletServerTest, TestSnapshotScan) {
  int num_rows = AllowSlowTests() ? 1000 : 100;
  int num_batches = AllowSlowTests() ? 100 : 10;
  vector<uint64_t> write_hybrid_times_collector;

  // perform a series of writes and collect the hybrid_times
  InsertTestRowsRemote(0, 0, num_rows, num_batches, nullptr,
                       kTabletId, &write_hybrid_times_collector);

  // now perform snapshot scans.
  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  int batch_idx = 1;
  for (uint64_t write_hybrid_time : write_hybrid_times_collector) {
    req.Clear();
    resp.Clear();
    rpc.Reset();
    // Set up a new request with no predicates, all columns.
    const Schema& projection = schema_;
    NewScanRequestPB* scan = req.mutable_new_scan_request();
    scan->set_tablet_id(kTabletId);
    scan->set_read_mode(READ_AT_SNAPSHOT);

    // Decode and re-encode the hybrid_time. Note that a snapshot at 'write_hybrid_time'
    // does not include the written rows, so we increment that hybrid_time by one
    // to make sure we get those rows back
    HybridTime read_hybrid_time(write_hybrid_time);
    read_hybrid_time = HybridTime(read_hybrid_time.value() + 1);
    scan->set_snap_hybrid_time(read_hybrid_time.ToUint64());

    ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
    req.set_call_seq_id(0);

    // Send the call
    {
      SCOPED_TRACE(req.DebugString());
      req.set_batch_size_bytes(0); // so it won't return data right away
      ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
      SCOPED_TRACE(resp.DebugString());
      ASSERT_FALSE(resp.has_error());
    }

    ASSERT_TRUE(resp.has_more_results());
    // Drain all the rows from the scanner.
    vector<string> results;
    ASSERT_NO_FATALS(DrainScannerToStrings(resp.scanner_id(), schema_, &results));
    // on each scan we should get (num_rows / num_batches) * batch_idx rows back
    int expected_num_rows = (num_rows / num_batches) * batch_idx;
    ASSERT_EQ(expected_num_rows, results.size());

    if (VLOG_IS_ON(2)) {
      VLOG(2) << "Scanner: " << resp.scanner_id() << " performing a snapshot read at: "
              << read_hybrid_time.ToString() << " got back: ";
      for (const string& result : results) {
        VLOG(2) << result;
      }
    }

    // assert that the first and last rows were the expected ones
    ASSERT_EQ("(int32 key=0, int32 int_val=0, string string_val=original0)", results[0]);
    ASSERT_EQ(Substitute("(int32 key=$0, int32 int_val=$0, string string_val=original$0)",
                         (batch_idx * (num_rows / num_batches) - 1)), results[results.size() - 1]);
    batch_idx++;
  }
}

TEST_F(TabletServerTest, TestSnapshotScan_WithoutSnapshotHybridTime) {
  vector<uint64_t> write_hybrid_times_collector;
  // perform a write
  InsertTestRowsRemote(0, 0, 1, 1, nullptr, kTabletId, &write_hybrid_times_collector);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  req.set_batch_size_bytes(0); // so it won't return data right away
  scan->set_read_mode(READ_AT_SNAPSHOT);

  HybridTime now = mini_server_->server()->clock()->Now();

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    req.set_batch_size_bytes(0); // so it won't return data right away
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // make sure that the snapshot hybrid_time that was selected is >= now
  ASSERT_GE(resp.snap_hybrid_time(), now.ToUint64());
}

// Tests that a snapshot in the future (beyond the current time plus maximum
// synchronization error) fails as an invalid snapshot.
TEST_F(TabletServerTest, TestSnapshotScan_SnapshotInTheFutureFails) {
  vector<uint64_t> write_hybrid_times_collector;
  // perform a write
  InsertTestRowsRemote(0, 0, 1, 1, nullptr, kTabletId, &write_hybrid_times_collector);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  req.set_batch_size_bytes(0); // so it won't return data right away
  scan->set_read_mode(READ_AT_SNAPSHOT);

  HybridTime read_hybrid_time(write_hybrid_times_collector[0]);
  // Increment the write hybrid_time by 60 secs: the server will definitely consider
  // this in the future.
  read_hybrid_time = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(read_hybrid_time) + 60000000);
  scan->set_snap_hybrid_time(read_hybrid_time.ToUint64());

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::INVALID_SNAPSHOT, resp.error().code());
  }
}

// Test tserver shutdown with an active scanner open.
TEST_F(TabletServerTest, TestSnapshotScan_OpenScanner) {
  vector<uint64_t> write_hybrid_times_collector;
  // Write and flush and write, so we have some rows in MRS and DRS
  InsertTestRowsRemote(0, 0, 100, 2, nullptr, kTabletId, &write_hybrid_times_collector);
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  InsertTestRowsRemote(0, 100, 100, 2, nullptr, kTabletId, &write_hybrid_times_collector);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  req.set_batch_size_bytes(0);
  scan->set_read_mode(READ_AT_SNAPSHOT);

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }
  // Intentionally do not drain the scanner at the end, to leave it open.
  // This tests tablet server shutdown with an active scanner.
}

// Test retrying a snapshot scan using last_row.
TEST_F(TabletServerTest, TestSnapshotScan_LastRow) {
  // Set the internal batching within the tserver to be small. Otherwise,
  // even though we use a small batch size in our request, we'd end up reading
  // many rows at a time.
  FLAGS_scanner_batch_size_rows = 5;
  const int num_rows = AllowSlowTests() ? 1000 : 100;
  const int num_batches = AllowSlowTests() ? 10 : 5;
  const int batch_size = num_rows / num_batches;

  // Generate some interleaved rows
  for (int i = 0; i < batch_size; i++) {
    ASSERT_OK(tablet_peer_->tablet()->Flush());
    for (int j = 0; j < num_rows; j++) {
      if (j % batch_size == i) {
        InsertTestRowsDirect(j, 1);
      }
    }
  }

  // Remove all the key columns from the projection.
  // This makes sure the scanner adds them in for sorting but removes them before returning
  // to the client.
  SchemaBuilder sb(schema_);
  for (int i = 0; i < schema_.num_key_columns(); i++) {
    ASSERT_OK(sb.RemoveColumn(schema_.column(i).name()));
  }
  const Schema& projection = sb.BuildWithoutIds();

  // Scan the whole tablet with a few different batch sizes.
  for (int i = 1; i < 10000; i *= 2) {
    ScanResponsePB resp;
    ScanRequestPB req;
    RpcController rpc;

    // Set up a new snapshot scan without a specified hybrid_time.
    NewScanRequestPB* scan = req.mutable_new_scan_request();
    scan->set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
    req.set_call_seq_id(0);
    scan->set_read_mode(READ_AT_SNAPSHOT);
    scan->set_order_mode(ORDERED);

    // Send the call
    {
      SCOPED_TRACE(req.DebugString());
      req.set_batch_size_bytes(0); // so it won't return data right away
      ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
      SCOPED_TRACE(resp.DebugString());
      ASSERT_FALSE(resp.has_error());
    }

    vector<string> results;
    do {
      rpc.Reset();
      // Send the call.
      {
        SCOPED_TRACE(req.DebugString());
        req.set_batch_size_bytes(i);
        ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
        SCOPED_TRACE(resp.DebugString());
        ASSERT_FALSE(resp.has_error());
      }
      // Save the rows into 'results' vector.
      StringifyRowsFromResponse(projection, rpc, resp, &results);
      // Retry the scan, setting the last_row_key and snapshot based on the response.
      scan->set_last_primary_key(resp.last_primary_key());
      scan->set_snap_hybrid_time(resp.snap_hybrid_time());
    } while (resp.has_more_results());

    ASSERT_EQ(num_rows, results.size());

    // Verify that we get the rows back in order.
    YBPartialRow row(&projection);
    for (int j = 0; j < num_rows; j++) {
      ASSERT_OK(row.SetInt32(0, j * 2));
      ASSERT_OK(row.SetStringCopy(1, StringPrintf("hello %d", j)));
      string expected = "(" + row.ToString() + ")";
      ASSERT_EQ(expected, results[j]);
    }
  }
}

// Tests that a read in the future succeeds if a propagated_hybrid_time (that is even
// further in the future) follows along. Also tests that the clock was updated so
// that no writes will ever have a hybrid_time post this snapshot.
TEST_F(TabletServerTest, TestSnapshotScan_SnapshotInTheFutureWithPropagatedHybridTime) {
  vector<uint64_t> write_hybrid_times_collector;
  // perform a write
  InsertTestRowsRemote(0, 0, 1, 1, nullptr, kTabletId, &write_hybrid_times_collector);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  req.set_batch_size_bytes(0); // so it won't return data right away
  scan->set_read_mode(READ_AT_SNAPSHOT);

  HybridTime read_hybrid_time(write_hybrid_times_collector[0]);
  // increment the write hybrid_time by 5 secs, the server will definitely consider
  // this in the future.
  read_hybrid_time = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(read_hybrid_time) + 5000000);
  scan->set_snap_hybrid_time(read_hybrid_time.ToUint64());

  // send a propagated hybrid_time that is an additional 100 msecs into the future.
  HybridTime propagated_hybrid_time = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(read_hybrid_time) + 100000);
  scan->set_propagated_hybrid_time(propagated_hybrid_time.ToUint64());

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // make sure the server's current clock returns a value that is larger than the
  // propagated hybrid_time. It should have the same physical time, but higher
  // logical time (due to various calls to clock.Now() when processing the request).
  HybridTime now = mini_server_->server()->clock()->Now();

  ASSERT_EQ(HybridClock::GetPhysicalValueMicros(propagated_hybrid_time),
            HybridClock::GetPhysicalValueMicros(now));

  ASSERT_GT(HybridClock::GetLogicalValue(now),
            HybridClock::GetLogicalValue(propagated_hybrid_time));

  vector<string> results;
  ASSERT_NO_FATALS(DrainScannerToStrings(resp.scanner_id(), schema_, &results));
  ASSERT_EQ(1, results.size());
  ASSERT_EQ("(int32 key=0, int32 int_val=0, string string_val=original0)", results[0]);
}

// Test that a read in the future fails, even if a propagated_hybrid_time is sent along,
// if the read_hybrid_time is beyond the propagated_hybrid_time.
TEST_F(TabletServerTest, TestSnapshotScan__SnapshotInTheFutureBeyondPropagatedHybridTimeFails) {
  FLAGS_disable_clock_sync_error = false;
  vector<uint64_t> write_hybrid_times_collector;
  // perform a write
  InsertTestRowsRemote(0, 0, 1, 1, nullptr, kTabletId, &write_hybrid_times_collector);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  req.set_batch_size_bytes(0); // so it won't return data right away
  scan->set_read_mode(READ_AT_SNAPSHOT);

  HybridTime read_hybrid_time(write_hybrid_times_collector[0]);
  // increment the write hybrid_time by 60 secs, the server will definitely consider
  // this in the future.
  read_hybrid_time = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(read_hybrid_time) + 60000000);
  scan->set_snap_hybrid_time(read_hybrid_time.ToUint64());

  // send a propagated hybrid_time that is an less than the read hybrid_time (but still
  // in the future as far the server is concerned).
  HybridTime propagated_hybrid_time = HybridClock::HybridTimeFromMicroseconds(
      HybridClock::GetPhysicalValueMicros(read_hybrid_time) - 100000);
  scan->set_propagated_hybrid_time(propagated_hybrid_time.ToUint64());

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::INVALID_SNAPSHOT, resp.error().code());
  }
}

TEST_F(TabletServerTest, TestScanWithStringPredicates) {
  InsertTestRowsDirect(0, 100);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  req.set_batch_size_bytes(0); // so it won't return data right away
  ASSERT_OK(SchemaToColumnPBs(schema_, scan->mutable_projected_columns()));

  // Set up a range predicate: "hello 50" < string_val <= "hello 59"
  ColumnRangePredicatePB* pred = scan->add_range_predicates();
  pred->mutable_column()->CopyFrom(scan->projected_columns(2));

  pred->set_lower_bound("hello 50");
  pred->set_upper_bound("hello 59");

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // Drain all the rows from the scanner.
  vector<string> results;
  ASSERT_NO_FATALS(
    DrainScannerToStrings(resp.scanner_id(), schema_, &results));
  ASSERT_EQ(10, results.size());
  ASSERT_EQ("(int32 key=50, int32 int_val=100, string string_val=hello 50)", results[0]);
  ASSERT_EQ("(int32 key=59, int32 int_val=118, string string_val=hello 59)", results[9]);
}

TEST_F(TabletServerTest, TestScanWithPredicates) {
  // TODO: need to test adding a predicate on a column which isn't part of the
  // projection! I don't think we implemented this at the tablet layer yet,
  // but should do so.

  int num_rows = AllowSlowTests() ? 10000 : 1000;
  InsertTestRowsDirect(0, num_rows);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  req.set_batch_size_bytes(0); // so it won't return data right away
  ASSERT_OK(SchemaToColumnPBs(schema_, scan->mutable_projected_columns()));

  // Set up a range predicate: 51 <= key <= 100
  ColumnRangePredicatePB* pred = scan->add_range_predicates();
  pred->mutable_column()->CopyFrom(scan->projected_columns(0));

  int32_t lower_bound_int = 51;
  int32_t upper_bound_int = 100;
  pred->mutable_lower_bound()->append(reinterpret_cast<char*>(&lower_bound_int),
                                      sizeof(lower_bound_int));
  pred->mutable_upper_bound()->append(reinterpret_cast<char*>(&upper_bound_int),
                                      sizeof(upper_bound_int));

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // Drain all the rows from the scanner.
  vector<string> results;
  ASSERT_NO_FATALS(
    DrainScannerToStrings(resp.scanner_id(), schema_, &results));
  ASSERT_EQ(50, results.size());
}

TEST_F(TabletServerTest, TestScanWithEncodedPredicates) {
  InsertTestRowsDirect(0, 100);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  req.set_batch_size_bytes(0); // so it won't return data right away
  ASSERT_OK(SchemaToColumnPBs(schema_, scan->mutable_projected_columns()));

  // Set up a range predicate: 51 <= key <= 60
  // using encoded keys
  int32_t start_key_int = 51;
  int32_t stop_key_int = 60;
  EncodedKeyBuilder ekb(&schema_);
  ekb.AddColumnKey(&start_key_int);
  gscoped_ptr<EncodedKey> start_encoded(ekb.BuildEncodedKey());

  ekb.Reset();
  ekb.AddColumnKey(&stop_key_int);
  gscoped_ptr<EncodedKey> stop_encoded(ekb.BuildEncodedKey());

  scan->mutable_start_primary_key()->assign(
    reinterpret_cast<const char*>(start_encoded->encoded_key().data()),
    start_encoded->encoded_key().size());
  scan->mutable_stop_primary_key()->assign(
    reinterpret_cast<const char*>(stop_encoded->encoded_key().data()),
    stop_encoded->encoded_key().size());

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // Drain all the rows from the scanner.
  vector<string> results;
  ASSERT_NO_FATALS(
    DrainScannerToStrings(resp.scanner_id(), schema_, &results));
  ASSERT_EQ(9, results.size());
  EXPECT_EQ("(int32 key=51, int32 int_val=102, string string_val=hello 51)",
            results.front());
  EXPECT_EQ("(int32 key=59, int32 int_val=118, string string_val=hello 59)",
            results.back());
}

// Test requesting more rows from a scanner which doesn't exist
TEST_F(TabletServerTest, TestBadScannerID) {
  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  req.set_scanner_id("does-not-exist");

  SCOPED_TRACE(req.DebugString());
  ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(TabletServerErrorPB::SCANNER_EXPIRED, resp.error().code());
}

// Test passing a scanner ID, but also filling in some of the NewScanRequest
// field.
TEST_F(TabletServerTest, TestInvalidScanRequest_NewScanAndScannerID) {
  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  req.set_batch_size_bytes(0); // so it won't return data right away
  req.set_scanner_id("x");
  SCOPED_TRACE(req.DebugString());
  Status s = proxy_->Scan(req, &resp, &rpc);
  ASSERT_FALSE(s.ok());
  ASSERT_STR_CONTAINS(s.ToString(), "Must not pass both a scanner_id and new_scan_request");
}

// Test that passing a projection with fields not present in the tablet schema
// throws an exception.
TEST_F(TabletServerTest, TestInvalidScanRequest_BadProjection) {
  const Schema projection({ ColumnSchema("col_doesnt_exist", INT32) }, 0);
  VerifyScanRequestFailure(projection,
                           TabletServerErrorPB::MISMATCHED_SCHEMA,
                           "Some columns are not present in the current schema: col_doesnt_exist");
}

// Test that passing a projection with mismatched type/nullability throws an exception.
TEST_F(TabletServerTest, TestInvalidScanRequest_BadProjectionTypes) {
  Schema projection;

  // Verify mismatched nullability for the not-null int field
  ASSERT_OK(
    projection.Reset({ ColumnSchema("int_val", INT32, true) }, // should be NOT NULL
                     0));
  VerifyScanRequestFailure(projection,
                           TabletServerErrorPB::MISMATCHED_SCHEMA,
                           "The column 'int_val' must have type int32 NOT "
                           "NULL found int32 NULLABLE");

  // Verify mismatched nullability for the nullable string field
  ASSERT_OK(
    projection.Reset({ ColumnSchema("string_val", STRING, false) }, // should be NULLABLE
                     0));
  VerifyScanRequestFailure(projection,
                           TabletServerErrorPB::MISMATCHED_SCHEMA,
                           "The column 'string_val' must have type string "
                           "NULLABLE found string NOT NULL");

  // Verify mismatched type for the not-null int field
  ASSERT_OK(
    projection.Reset({ ColumnSchema("int_val", INT16, false) },     // should be INT32 NOT NULL
                     0));
  VerifyScanRequestFailure(projection,
                           TabletServerErrorPB::MISMATCHED_SCHEMA,
                           "The column 'int_val' must have type int32 NOT "
                           "NULL found int16 NOT NULL");

  // Verify mismatched type for the nullable string field
  ASSERT_OK(projection.Reset(
        { ColumnSchema("string_val", INT32, true) }, // should be STRING NULLABLE
        0));
  VerifyScanRequestFailure(projection,
                           TabletServerErrorPB::MISMATCHED_SCHEMA,
                           "The column 'string_val' must have type string "
                           "NULLABLE found int32 NULLABLE");
}

// Test that passing a projection with Column IDs throws an exception.
// Column IDs are assigned to the user request schema on the tablet server
// based on the latest schema.
TEST_F(TabletServerTest, TestInvalidScanRequest_WithIds) {
  const Schema* projection = tablet_peer_->tablet()->schema();
  ASSERT_TRUE(projection->has_column_ids());
  VerifyScanRequestFailure(*projection,
                           TabletServerErrorPB::INVALID_SCHEMA,
                           "User requests should not have Column IDs");
}

// Test scanning a tablet that has no entries.
TEST_F(TabletServerTest, TestScan_NoResults) {
  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  // Set up a new request with no predicates, all columns.
  const Schema& projection = schema_;
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  req.set_batch_size_bytes(0); // so it won't return data right away
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());

    // Because there are no entries, we should immediately return "no results".
    ASSERT_FALSE(resp.has_more_results());
  }
}

// Test scanning a tablet that has no entries.
TEST_F(TabletServerTest, TestScan_InvalidScanSeqId) {
  InsertTestRowsDirect(0, 10);

  ScanRequestPB req;
  ScanResponsePB resp;
  RpcController rpc;

  {
    // Set up a new scan request with no predicates, all columns.
    const Schema& projection = schema_;
    NewScanRequestPB* scan = req.mutable_new_scan_request();
    scan->set_tablet_id(kTabletId);
    ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
    req.set_call_seq_id(0);
    req.set_batch_size_bytes(0); // so it won't return data right away

    // Create the scanner
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error());
    ASSERT_TRUE(resp.has_more_results());
  }

  string scanner_id = resp.scanner_id();
  resp.Clear();

  {
    // Continue the scan with an invalid sequence ID
    req.Clear();
    rpc.Reset();
    req.set_scanner_id(scanner_id);
    req.set_batch_size_bytes(0); // so it won't return data right away
    req.set_call_seq_id(42); // should be 1

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::INVALID_SCAN_CALL_SEQ_ID, resp.error().code());
  }
}

void TabletServerTest::DoOrderedScanTest(const Schema& projection,
                                         const string& expected_rows_as_string) {
  InsertTestRowsDirect(0, 10);
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  InsertTestRowsDirect(10, 10);
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  InsertTestRowsDirect(20, 10);

  ScanResponsePB resp;
  ScanRequestPB req;
  RpcController rpc;

  // Set up a new snapshot scan without a specified hybrid_time.
  NewScanRequestPB* scan = req.mutable_new_scan_request();
  scan->set_tablet_id(kTabletId);
  ASSERT_OK(SchemaToColumnPBs(projection, scan->mutable_projected_columns()));
  req.set_call_seq_id(0);
  scan->set_read_mode(READ_AT_SNAPSHOT);
  scan->set_order_mode(ORDERED);

  {
    SCOPED_TRACE(req.DebugString());
    req.set_batch_size_bytes(0); // so it won't return data right away
    ASSERT_OK(proxy_->Scan(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  vector<string> results;
  ASSERT_NO_FATALS(
    DrainScannerToStrings(resp.scanner_id(), projection, &results));

  ASSERT_EQ(30, results.size());

  for (int i = 0; i < results.size(); ++i) {
    ASSERT_EQ(results[i], Substitute(expected_rows_as_string, i, i * 2));
  }
}

// Tests for KUDU-967. This test creates multiple row sets and then performs an ordered
// scan including the key columns in the projection but without marking them as keys.
// Without a fix for KUDU-967 the scan will often return out-of-order results.
TEST_F(TabletServerTest, TestOrderedScan_ProjectionWithKeyColumnsInOrder) {
  // Build a projection with all the columns, but don't mark the key columns as such.
  SchemaBuilder sb;
  for (int i = 0; i < schema_.num_columns(); i++) {
    ASSERT_OK(sb.AddColumn(schema_.column(i), false));
  }
  const Schema& projection = sb.BuildWithoutIds();
  DoOrderedScanTest(projection, "(int32 key=$0, int32 int_val=$1, string string_val=hello $0)");
}

// Same as above but doesn't add the key columns to the projection.
TEST_F(TabletServerTest, TestOrderedScan_ProjectionWithoutKeyColumns) {
  // Build a projection without the key columns.
  SchemaBuilder sb;
  for (int i = schema_.num_key_columns(); i < schema_.num_columns(); i++) {
    ASSERT_OK(sb.AddColumn(schema_.column(i), false));
  }
  const Schema& projection = sb.BuildWithoutIds();
  DoOrderedScanTest(projection, "(int32 int_val=$1, string string_val=hello $0)");
}

// Same as above but creates a projection with the order of columns reversed.
TEST_F(TabletServerTest, TestOrderedScan_ProjectionWithKeyColumnsOutOfOrder) {
  // Build a projection with the order of the columns reversed.
  SchemaBuilder sb;
  for (int i = schema_.num_columns() - 1; i >= 0; i--) {
    ASSERT_OK(sb.AddColumn(schema_.column(i), false));
  }
  const Schema& projection = sb.BuildWithoutIds();
  DoOrderedScanTest(projection, "(string string_val=hello $0, int32 int_val=$1, int32 key=$0)");
}

TEST_F(TabletServerTest, TestCreateTablet_TabletExists) {
  CreateTabletRequestPB req;
  CreateTabletResponsePB resp;
  RpcController rpc;

  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_table_id("testtb");
  req.set_tablet_id(kTabletId);
  PartitionPB* partition = req.mutable_partition();
  partition->set_partition_key_start(" ");
  partition->set_partition_key_end(" ");
  req.set_table_name("testtb");
  req.mutable_config()->CopyFrom(mini_server_->CreateLocalConfig());

  Schema schema = SchemaBuilder(schema_).Build();
  ASSERT_OK(SchemaToPB(schema, req.mutable_schema()));

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(admin_proxy_->CreateTablet(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::TABLET_ALREADY_EXISTS, resp.error().code());
  }
}

TEST_F(TabletServerTest, TestDeleteTablet) {
  scoped_refptr<TabletPeer> tablet;

  // Verify that the tablet exists
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));

  // Put some data in the tablet. We flush and insert more rows to ensure that
  // there is data both in the MRS and on disk.
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 1));
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 2, 1));

  // Drop any local references to the tablet from within this test,
  // so that when we delete it on the server, it's not held alive
  // by the test code.
  tablet_peer_.reset();
  tablet.reset();

  DeleteTabletRequestPB req;
  DeleteTabletResponsePB resp;
  RpcController rpc;

  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_tablet_id(kTabletId);
  req.set_delete_type(tablet::TABLET_DATA_DELETED);

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(admin_proxy_->DeleteTablet(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // Verify that the tablet is removed from the tablet map
  ASSERT_FALSE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));

  // Verify that fetching metrics doesn't crash. Regression test for KUDU-638.
  EasyCurl c;
  faststring buf;
  ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/jsonmetricz",
                                           ToString(mini_server_->bound_http_addr())),
                                           &buf));

  // Verify that after restarting the TS, the tablet is still not in the tablet manager.
  // This ensures that the on-disk metadata got removed.
  Status s = ShutdownAndRebuildTablet();
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
  ASSERT_FALSE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));
}

TEST_F(TabletServerTest, TestDeleteTablet_TabletNotCreated) {
  DeleteTabletRequestPB req;
  DeleteTabletResponsePB resp;
  RpcController rpc;

  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_tablet_id("NotPresentTabletId");
  req.set_delete_type(tablet::TABLET_DATA_DELETED);

  // Send the call
  {
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(admin_proxy_->DeleteTablet(req, &resp, &rpc));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_TRUE(resp.has_error());
    ASSERT_EQ(TabletServerErrorPB::TABLET_NOT_FOUND, resp.error().code());
  }
}

// Test that with concurrent requests to delete the same tablet, one wins and
// the other fails, with no assertion failures. Regression test for KUDU-345.
TEST_F(TabletServerTest, TestConcurrentDeleteTablet) {
  // Verify that the tablet exists
  scoped_refptr<TabletPeer> tablet;
  ASSERT_TRUE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));

  static const int kNumDeletes = 2;
  RpcController rpcs[kNumDeletes];
  DeleteTabletResponsePB responses[kNumDeletes];
  CountDownLatch latch(kNumDeletes);

  DeleteTabletRequestPB req;
  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_tablet_id(kTabletId);
  req.set_delete_type(tablet::TABLET_DATA_DELETED);

  for (int i = 0; i < kNumDeletes; i++) {
    SCOPED_TRACE(req.DebugString());
    admin_proxy_->DeleteTabletAsync(
        req, &responses[i], &rpcs[i], [&latch]() { latch.CountDown(); });
  }
  latch.Wait();

  int num_success = 0;
  for (int i = 0; i < kNumDeletes; i++) {
    ASSERT_TRUE(rpcs[i].finished());
    LOG(INFO) << "STATUS " << i << ": " << rpcs[i].status().ToString();
    LOG(INFO) << "RESPONSE " << i << ": " << responses[i].DebugString();
    if (!responses[i].has_error()) {
      num_success++;
    }
  }

  // Verify that the tablet is removed from the tablet map
  ASSERT_FALSE(mini_server_->server()->tablet_manager()->LookupTablet(kTabletId, &tablet));
  ASSERT_EQ(1, num_success);
}

TEST_F(TabletServerTest, TestInsertLatencyMicroBenchmark) {
  METRIC_DEFINE_entity(test);
  METRIC_DEFINE_histogram(test, insert_latency,
                          "Insert Latency",
                          MetricUnit::kMicroseconds,
                          "TabletServer single threaded insert latency.",
                          10000000,
                          2);

  scoped_refptr<Histogram> histogram = METRIC_insert_latency.Instantiate(ts_test_metric_entity_);

  uint64_t warmup = AllowSlowTests() ?
      FLAGS_single_threaded_insert_latency_bench_warmup_rows : 10;

  for (int i = 0; i < warmup; i++) {
    InsertTestRowsRemote(0, i, 1);
  }

  uint64_t max_rows = AllowSlowTests() ?
      FLAGS_single_threaded_insert_latency_bench_insert_rows : 100;

  MonoTime start = MonoTime::Now(MonoTime::FINE);

  for (int i = warmup; i < warmup + max_rows; i++) {
    MonoTime before = MonoTime::Now(MonoTime::FINE);
    InsertTestRowsRemote(0, i, 1);
    MonoTime after = MonoTime::Now(MonoTime::FINE);
    MonoDelta delta = after.GetDeltaSince(before);
    histogram->Increment(delta.ToMicroseconds());
  }

  MonoTime end = MonoTime::Now(MonoTime::FINE);
  double throughput = ((max_rows - warmup) * 1.0) / end.GetDeltaSince(start).ToSeconds();

  // Generate the JSON.
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(histogram->WriteAsJson(&writer, MetricJsonOptions()));

  LOG(INFO) << "Throughput: " << throughput << " rows/sec.";
  LOG(INFO) << out.str();
}

// Simple test to ensure we can destroy an RpcServer in different states of
// initialization before Start()ing it.
TEST_F(TabletServerTest, TestRpcServerCreateDestroy) {
  RpcServerOptions opts;
  {
    RpcServer server1("server1", opts);
  }
  {
    RpcServer server2("server2", opts);
    MessengerBuilder mb("foo");
    shared_ptr<Messenger> messenger;
    ASSERT_OK(mb.Build(&messenger));
    ASSERT_OK(server2.Init(messenger));
  }
}

// Simple test to ensure we can create RpcServer with different bind address options.
TEST_F(TabletServerTest, TestRpcServerRPCFlag) {
  FLAGS_rpc_bind_addresses = "0.0.0.0:2000";
  RpcServerOptions opts;
  ServerRegistrationPB reg;
  TabletServerOptions tbo;
  MessengerBuilder mb("foo");
  shared_ptr<Messenger> messenger;
  ASSERT_OK(mb.Build(&messenger));

  RpcServer server1("server1", opts);
  ASSERT_OK(server1.Init(messenger));

  FLAGS_rpc_bind_addresses = "0.0.0.0:2000,0.0.0.1:2001";
  RpcServerOptions opts2;
  RpcServer server2("server2", opts2);
  ASSERT_OK(server2.Init(messenger));

  FLAGS_rpc_bind_addresses = "10.20.30.40:2017";
  RpcServerOptions opts3;
  RpcServer server3("server3", opts3);
  ASSERT_OK(server3.Init(messenger));

  reg.Clear();
  tbo.rpc_opts = opts3;
  TabletServer server(tbo);
  ASSERT_NO_FATALS(WARN_NOT_OK(server.Init(), "Ignore"));
  // This call will fail for http binding, but this test is for rpc.
  ASSERT_NO_FATALS(WARN_NOT_OK(server.GetRegistration(&reg), "Ignore"));
  ASSERT_EQ(1, reg.rpc_addresses().size());
  ASSERT_EQ("10.20.30.40", reg.rpc_addresses(0).host());
  ASSERT_EQ(2017, reg.rpc_addresses(0).port());

  reg.Clear();
  FLAGS_rpc_bind_addresses = "10.20.30.40:2017,20.30.40.50:2018";
  RpcServerOptions opts4;
  tbo.rpc_opts = opts4;
  TabletServer tserver2(tbo);
  ASSERT_NO_FATALS(WARN_NOT_OK(tserver2.Init(), "Ignore"));
  // This call will fail for http binding, but this test is for rpc.
  ASSERT_NO_FATALS(WARN_NOT_OK(tserver2.GetRegistration(&reg), "Ignore"));
  ASSERT_EQ(2, reg.rpc_addresses().size());
}

// We are not checking if a row is out of bounds in YB because we are using a completely different
// hash-based partitioning scheme, so this test is now actually testing that there is no error
// returned from the server. If we introduce such range checking, this test could be enhanced to
// test for it.
TEST_F(TabletServerTest, TestWriteOutOfBounds) {
  const char *tabletId = "TestWriteOutOfBoundsTablet";
  Schema schema = SchemaBuilder(schema_).Build();

  PartitionSchema partition_schema;
  CHECK_OK(PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  YBPartialRow start_row(&schema);
  ASSERT_OK(start_row.SetInt32("key", 10));

  YBPartialRow end_row(&schema);
  ASSERT_OK(end_row.SetInt32("key", 20));

  vector<Partition> partitions;
  ASSERT_OK(partition_schema.CreatePartitions({ start_row, end_row }, schema, &partitions));

  ASSERT_EQ(3, partitions.size());

  ASSERT_OK(
    mini_server_->server()->tablet_manager()->CreateNewTablet("TestWriteOutOfBoundsTable", tabletId,
      partitions[1], tabletId, YQL_TABLE_TYPE, schema, partition_schema,
      mini_server_->CreateLocalConfig(), nullptr));

  ASSERT_OK(WaitForTabletRunning(tabletId));

  WriteRequestPB req;
  WriteResponsePB resp;
  RpcController controller;
  req.set_tablet_id(tabletId);
  ASSERT_OK(SchemaToPB(schema_, req.mutable_schema()));

  vector<RowOperationsPB::Type> ops = { RowOperationsPB::INSERT, RowOperationsPB::UPDATE };

  for (const RowOperationsPB::Type &op : ops) {
    RowOperationsPB* data = req.mutable_row_operations();
    AddTestRowToPB(op, schema_, 20, 1, "1", data);
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    data->Clear();
    controller.Reset();
  }
}

static uint32_t CalcTestRowChecksum(int32_t key, uint8_t string_field_defined = true) {
  crc::Crc* crc = crc::GetCrc32cInstance();
  uint64_t row_crc = 0;

  string strval = strings::Substitute("original$0", key);
  uint32_t index = 0;
  crc->Compute(&index, sizeof(index), &row_crc, nullptr);
  crc->Compute(&key, sizeof(int32_t), &row_crc, nullptr);

  index = 1;
  crc->Compute(&index, sizeof(index), &row_crc, nullptr);
  crc->Compute(&key, sizeof(int32_t), &row_crc, nullptr);

  index = 2;
  crc->Compute(&index, sizeof(index), &row_crc, nullptr);
  crc->Compute(&string_field_defined, sizeof(string_field_defined), &row_crc, nullptr);
  if (string_field_defined) {
    crc->Compute(strval.c_str(), strval.size(), &row_crc, nullptr);
  }
  return static_cast<uint32_t>(row_crc);
}

// Simple test to check that our checksum scans work as expected.
TEST_F(TabletServerTest, TestChecksumScan) {
  uint64_t total_crc = 0;

  ChecksumRequestPB req;
  req.mutable_new_request()->set_tablet_id(kTabletId);
  req.mutable_new_request()->set_read_mode(READ_LATEST);
  req.set_call_seq_id(0);
  ASSERT_OK(SchemaToColumnPBs(schema_, req.mutable_new_request()->mutable_projected_columns(),
                              SCHEMA_PB_WITHOUT_IDS));
  ChecksumRequestPB new_req = req;  // Cache "new" request.

  ChecksumResponsePB resp;
  RpcController controller;
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));

  // No rows.
  ASSERT_EQ(total_crc, resp.checksum());
  ASSERT_FALSE(resp.has_more_results());

  // First row.
  int32_t key = 1;
  InsertTestRowsRemote(0, key, 1);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  total_crc += CalcTestRowChecksum(key);
  uint64_t first_crc = total_crc; // Cache first record checksum.

  ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
  ASSERT_EQ(total_crc, resp.checksum());
  ASSERT_FALSE(resp.has_more_results());

  // Second row (null string field).
  key = 2;
  InsertTestRowsRemote(0, key, 1, 1, nullptr, kTabletId, nullptr, nullptr, false);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  total_crc += CalcTestRowChecksum(key, false);

  ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
  ASSERT_EQ(total_crc, resp.checksum());
  ASSERT_FALSE(resp.has_more_results());

  // Now test the same thing, but with a scan requiring 2 passes (one per row).
  FLAGS_scanner_batch_size_rows = 1;
  req.set_batch_size_bytes(1);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  string scanner_id = resp.scanner_id();
  ASSERT_TRUE(resp.has_more_results());
  uint64_t agg_checksum = resp.checksum();

  // Second row.
  req.clear_new_request();
  req.mutable_continue_request()->set_scanner_id(scanner_id);
  req.mutable_continue_request()->set_previous_checksum(agg_checksum);
  req.set_call_seq_id(1);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  ASSERT_EQ(total_crc, resp.checksum());
  ASSERT_FALSE(resp.has_more_results());

  // Finally, delete row 2, so we're back to the row 1 checksum.
  ASSERT_NO_FATALS(DeleteTestRowsRemote(key, 1));
  FLAGS_scanner_batch_size_rows = 100;
  req = new_req;
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  ASSERT_NE(total_crc, resp.checksum());
  ASSERT_EQ(first_crc, resp.checksum());
  ASSERT_FALSE(resp.has_more_results());
}

class DelayFsyncLogHook : public log::Log::LogFaultHooks {
 public:
  DelayFsyncLogHook() : log_latch1_(1), test_latch1_(1) {}

  Status PostAppend() override {
    test_latch1_.CountDown();
    log_latch1_.Wait();
    log_latch1_.Reset(1);
    return Status::OK();
  }

  void Continue() {
    test_latch1_.Wait();
    log_latch1_.CountDown();
  }

 private:
  CountDownLatch log_latch1_;
  CountDownLatch test_latch1_;
};

} // namespace tserver
} // namespace yb
