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

#include "yb/qlexpr/index.h"
#include "yb/dockv/partition.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema_pbutil.h"

#include "yb/consensus/log-test-base.h"

#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_test_util.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/server/hybrid_clock.h"
#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server-test-base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_server_test_util.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_call_home.h"
#include "yb/server/call_home-test-util.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/crc.h"
#include "yb/util/curl_util.h"
#include "yb/util/metrics.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"
#include "yb/util/monotime.h"

using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using yb::server::HybridClock;
using std::string;
using strings::Substitute;

DEFINE_NON_RUNTIME_int32(single_threaded_insert_latency_bench_warmup_rows, 100,
             "Number of rows to insert in the warmup phase of the single threaded"
             " tablet server insert latency micro-benchmark");

DEFINE_NON_RUNTIME_int32(single_threaded_insert_latency_bench_insert_rows, 1000,
             "Number of rows to insert in the testing phase of the single threaded"
             " tablet server insert latency micro-benchmark");

DECLARE_int32(metrics_retirement_age_ms);
DECLARE_string(block_manager);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(disable_clock_sync_error);
DECLARE_string(metric_node_name);

// Declare these metrics prototypes for simpler unit testing of their behavior.
METRIC_DECLARE_counter(rows_inserted);
METRIC_DECLARE_counter(rows_updated);
METRIC_DECLARE_counter(rows_deleted);
METRIC_DECLARE_gauge_uint64(untracked_memory);

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

  Status CallDeleteTablet(
      const string& uuid, const char* tablet_id, tablet::TabletDataState state) {
    DeleteTabletRequestPB req;
    DeleteTabletResponsePB resp;
    RpcController rpc;

    req.set_dest_uuid(uuid);
    req.set_tablet_id(tablet_id);
    req.set_delete_type(state);

    // Send the call
    {
      SCOPED_TRACE(req.DebugString());
      RETURN_NOT_OK(admin_proxy_->DeleteTablet(req, &resp, &rpc));
      SCOPED_TRACE(resp.DebugString());
      if (resp.has_error()) {
        auto status = StatusFromPB(resp.error().status());
        RETURN_NOT_OK(status);
      }
    }
    return Status::OK();
  }

  string GetWebserverDir() { return GetTestPath("webserver-docroot"); }
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
  ASSERT_GT(mini_server_->Now().ToUint64(), resp.hybrid_time());
}

TEST_F(TabletServerTest, TestSetFlagsAndCheckWebPages) {
  server::GenericServiceProxy proxy(
      proxy_cache_.get(), HostPort::FromBoundEndpoint(mini_server_->bound_rpc_addr()));

  server::SetFlagRequestPB req;
  server::SetFlagResponsePB resp;

  // Set an invalid flag.
  {
    RpcController controller;
    req.set_flag("foo");
    req.set_value("bar");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::NO_SUCH_FLAG, resp.result());
    ASSERT_EQ(resp.msg(), "Flag does not exist");
  }

  // Set a valid flag to a valid value.
  {
    int32_t old_val = FLAGS_metrics_retirement_age_ms;
    RpcController controller;
    req.set_flag("metrics_retirement_age_ms");
    req.set_value("12345");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::SUCCESS, resp.result());
    ASSERT_EQ(resp.msg(), "metrics_retirement_age_ms set to 12345\n");
    ASSERT_EQ(Substitute("$0", old_val), resp.old_value());
    ASSERT_EQ(12345, FLAGS_metrics_retirement_age_ms);
  }

  // Set a valid flag to an invalid value.
  {
    RpcController controller;
    req.set_flag("metrics_retirement_age_ms");
    req.set_value("foo");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::BAD_VALUE, resp.result());
    ASSERT_STR_CONTAINS(resp.msg(), "Unable to set flag: Bad value");
    ASSERT_EQ(12345, FLAGS_metrics_retirement_age_ms);
  }
  // Set a valid flag to an value that is rejected by its validator.
  {
    RpcController controller;
    req.set_flag("log_min_segments_to_retain");
    req.set_value("0");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::BAD_VALUE, resp.result());
    ASSERT_STR_CONTAINS(resp.msg(), "Must be greater than 0");
    ASSERT_EQ(12345, FLAGS_metrics_retirement_age_ms);
  }

  // Try setting a flag which isn't runtime-modifiable
  {
    RpcController controller;
    req.set_flag("tablet_do_compaction_cleanup_for_intents");
    req.set_value("true");
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::NOT_SAFE, resp.result());
  }

  // Try again, but with the force flag.
  {
    RpcController controller;
    req.set_flag("tablet_do_compaction_cleanup_for_intents");
    req.set_value("true");
    req.set_force(true);
    ASSERT_OK(proxy.SetFlag(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_EQ(server::SetFlagResponsePB::SUCCESS, resp.result());
  }

  EasyCurl c;
  faststring buf;
  string addr = yb::ToString(mini_server_->bound_http_addr());

  // Tablets page should list tablet.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tablets", addr),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), kTabletId);

  // Tablet page should include the schema.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/tablet?id=$1", addr, kTabletId),
                       &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), "<th>key</th>");
  ASSERT_STR_CONTAINS(buf.ToString(), "<td>string NULLABLE VALUE</td>");

  ASSERT_OK(c.FetchURL(Substitute("http://$0/tablet-consensus-status?id=$1",
                       addr, kTabletId), &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), kTabletId);

  // Test fetching metrics.
  // Fetching metrics has the side effect of retiring metrics, but not in a single pass.
  // So, we check a couple of times in a loop -- thus, if we had a bug where one of these
  // metrics was accidentally un-referenced too early, we'd cause it to get retired.
  // If the metrics survive several passes of fetching, then we are pretty sure they will
  // stick around properly for the whole lifetime of the server.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_retirement_age_ms) = 0;
  for (int i = 0; i < 3; i++) {
    SCOPED_TRACE(i);
    ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/jsonmetricz", addr, kTabletId),
                                &buf));

    // Check that the tablet entry shows up.
    ASSERT_STR_CONTAINS(buf.ToString(), "\"type\": \"tablet\"");
    ASSERT_STR_CONTAINS(buf.ToString(), "\"id\": \"test-tablet\"");

    // Check entity attributes.
    ASSERT_STR_CONTAINS(buf.ToString(), "\"table_name\": \"test-table\"");

    // Check for the existence of some particular metrics for which we've had early-retirement
    // bugs in the past.
    ASSERT_STR_CONTAINS(buf.ToString(), "hybrid_clock_hybrid_time");
    ASSERT_STR_CONTAINS(buf.ToString(), "threads_started");
#if YB_TCMALLOC_ENABLED
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

  // Test URL parameter: reset_histograms.
  // This parameter allow user to have percentile not to be reseted for every web page fetch.
  // Here, handler_latency_yb_tserver_TabletServerService_Write's percentile is used for testing.
  // In the begining, we expect it's value is zero.
  ASSERT_OK(c.FetchURL(Substitute("http://$0/prometheus-metrics?reset_histograms=false", addr),
                &buf));
  // Find our target metric and concatenate value zero to it. metric_instance_with_zero_value is a
  // string looks like: handler_latency_yb_tserver_TabletServerService_Write{quantile=p50.....} 0
  string page_content = buf.ToString();
  std::size_t begin = page_content.find("handler_latency_yb_tserver_TabletServerService_Write"
                                        "{quantile=\"p50\"");
  std::size_t end = page_content.find("}", begin);
  string metric_instance_with_zero_value = page_content.substr(begin, end - begin + 1) + " 0";

  ASSERT_STR_CONTAINS(buf.ToString(), metric_instance_with_zero_value);

  // Insert some data
  auto tablet = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  tablet.reset();
  WriteRequestPB w_req;
  w_req.set_tablet_id(kTabletId);
  WriteResponsePB w_resp;
  {
    RpcController controller;
    AddTestRowInsert(1234, 5678, "testing reset histograms via RPC", &w_req);
    SCOPED_TRACE(w_req.DebugString());
    ASSERT_OK(proxy_->Write(w_req, &w_resp, &controller));
    SCOPED_TRACE(w_resp.DebugString());
    ASSERT_FALSE(w_resp.has_error());
    w_req.clear_ql_write_batch();
  }

  // Check that its percentile become none zero after inserting data
  ASSERT_OK(c.FetchURL(Substitute("http://$0/prometheus-metrics?reset_histograms=false", addr),
                &buf));
  ASSERT_STR_NOT_CONTAINS(buf.ToString(), metric_instance_with_zero_value);

  // Check that percentile should not to be reseted to zero after refreshing the page
  ASSERT_OK(c.FetchURL(Substitute("http://$0/prometheus-metrics?reset_histograms=false", addr),
                &buf));
  ASSERT_STR_NOT_CONTAINS(buf.ToString(), metric_instance_with_zero_value);

  // Fetch the page again with reset_histograms=true to reset the percentile
  ASSERT_OK(c.FetchURL(Substitute("http://$0/prometheus-metrics?reset_histograms=true", addr),
                &buf));

  // Verify that the percentile has been reseted back to zero
  ASSERT_OK(c.FetchURL(Substitute("http://$0/prometheus-metrics?reset_histograms=true", addr),
                &buf));
  ASSERT_STR_CONTAINS(buf.ToString(), metric_instance_with_zero_value);
  tablet.reset();
}

TEST_F(TabletServerTest, TestInsert) {
  WriteRequestPB req;

  req.set_tablet_id(kTabletId);

  WriteResponsePB resp;
  RpcController controller;

  auto tablet = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  scoped_refptr<Counter> rows_inserted =
      METRIC_rows_inserted.Instantiate(tablet->tablet()->GetTabletMetricsEntity());
  ASSERT_EQ(0, rows_inserted->value());
  tablet.reset();

  // Send an empty request.
  // This should succeed and do nothing.
  {
    controller.Reset();
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
  }

  // Send an actual row insert.
  {
    controller.Reset();
    AddTestRowInsert(1234, 5678, "hello world via RPC", &req);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    req.clear_ql_write_batch();
  }

  // Send a batch with multiple rows, one of which is a duplicate of
  // the above insert. This should generate one error into per_row_errors.
  {
    controller.Reset();
    AddTestRowInsert(1, 1, "ceci n'est pas une dupe", &req);
    AddTestRowInsert(2, 1, "also not a dupe key", &req);
    AddTestRowInsert(1234, 1, "I am a duplicate key", &req);
    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
  }

  // get the clock's current hybrid_time
  HybridTime now_before = mini_server_->Now();

  rows_inserted = nullptr;
  ASSERT_OK(ShutdownAndRebuildTablet());
  VerifyRows(schema_, { KeyValue(1, 1), KeyValue(2, 1), KeyValue(1234, 5678) });

  // get the clock's hybrid_time after replay
  HybridTime now_after = mini_server_->Now();

  // make sure 'now_after' is greater than or equal to 'now_before'
  ASSERT_GE(now_after.value(), now_before.value());
}

TEST_F(TabletServerTest, TestExternalConsistencyModes_ClientPropagated) {
  WriteRequestPB req;
  req.set_tablet_id(kTabletId);
  WriteResponsePB resp;
  RpcController controller;

  auto tablet = ASSERT_RESULT(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));
  // Advance current to some time in the future. we do 5 secs to make
  // sure this hybrid_time will still be in the future when it reaches the
  // server.
  HybridTime current = mini_server_->Now().AddMicroseconds(5000000);

  AddTestRowInsert(1234, 5678, "hello world via RPC", &req);

  req.set_propagated_hybrid_time(current.ToUint64());
  SCOPED_TRACE(req.DebugString());
  ASSERT_OK(proxy_->Write(req, &resp, &controller));
  SCOPED_TRACE(resp.DebugString());
  ASSERT_FALSE(resp.has_error());

  // make sure the server returned a write hybrid_time where only
  // the logical value was increased since he should have updated
  // its clock with the client's value.
  HybridTime write_hybrid_time(resp.propagated_hybrid_time());

  ASSERT_EQ(current.GetPhysicalValueMicros(),
            write_hybrid_time.GetPhysicalValueMicros());

  ASSERT_LE(current.GetLogicalValue() + 1,
            write_hybrid_time.GetLogicalValue());
}

TEST_F(TabletServerTest, TestInsertAndMutate) {
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  RpcController controller;

  {
    WriteRequestPB req;
    WriteResponsePB resp;
    req.set_tablet_id(kTabletId);

    AddTestRowInsert(1, 1, "original1", &req);
    AddTestRowInsert(2, 2, "original2", &req);
    AddTestRowInsert(3, 3, "original3", &req);
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

    AddTestRowUpdate(1, 2, "mutation1", &req);
    AddTestRowUpdate(2, 3, "mutation2", &req);
    AddTestRowUpdate(3, 4, "mutation3", &req);
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

    AddTestRowUpdate(1234, 2, "mutated", &req);
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

    AddTestRowDelete(1, &req);
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

    AddTestRowUpdate(1, 2, "mutated1", &req);
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

    // op 0: Mutate row 1, which doesn't exist. This should not fail.
    AddTestRowUpdate(1, 3, "mutate_should_not_fail", &req);
    // op 1: Insert a new row 4 (succeeds)
    AddTestRowInsert(4, 4, "new row 4", &req);
    // op 2: Delete a non-existent row 5 (should fail)
    AddTestRowDelete(5, &req);
    // op 3: Insert a new row 6 (succeeds)
    AddTestRowInsert(6, 6, "new row 6", &req);

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error()) << resp.ShortDebugString();
    controller.Reset();
  }

  // get the clock's current hybrid_time
  HybridTime now_before = mini_server_->Now();

  ASSERT_NO_FATALS(WARN_NOT_OK(ShutdownAndRebuildTablet(), "Shutdown failed: "));
  VerifyRows(schema_,
      { KeyValue(1, 3), KeyValue(2, 3), KeyValue(3, 4), KeyValue(4, 4), KeyValue(6, 6),
        KeyValue(1234, 2) });

  // get the clock's hybrid_time after replay
  HybridTime now_after = mini_server_->Now();

  // make sure 'now_after' is greater that or equal to 'now_before'
  ASSERT_GE(now_after.value(), now_before.value());
}

// Test that passing a schema with fields not present in the tablet schema
// throws an exception.
TEST_F(TabletServerTest, TestInvalidWriteRequest_BadSchema) {
  SchemaBuilder schema_builder(schema_);
  ASSERT_OK(schema_builder.AddColumn("col_doesnt_exist", DataType::INT32));
  Schema bad_schema_with_ids = schema_builder.Build();
  Schema bad_schema = schema_builder.BuildWithoutIds();

  // Send a row insert with an extra column
  {
    WriteRequestPB req;
    WriteResponsePB resp;
    RpcController controller;

    req.set_tablet_id(kTabletId);

    AddTestRowInsert(1234, 5678, "hello world via RPC", &req);
    req.mutable_ql_write_batch(0)->set_schema_version(1);

    SCOPED_TRACE(req.DebugString());
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    ASSERT_EQ(QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH, resp.ql_response_batch(0).status());
  }
}

TEST_F(TabletServerTest, TestClientGetsErrorBackWhenRecoveryFailed) {
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 7));

  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));

  // Save the log path before shutting down the tablet (and destroying
  // the tablet peer).
  string log_path = tablet_peer_->log()->TEST_ActiveSegment()->path();
  auto idx = tablet_peer_->log()->TEST_ActiveSegment()->first_entry_offset() + 300;

  ShutdownTablet();
  ASSERT_OK(log::CorruptLogFile(env_.get(), log_path, log::FLIP_BYTE, idx));

  ASSERT_FALSE(ShutdownAndRebuildTablet().ok());

  // Connect to it.
  CreateTsClientProxies(HostPort::FromBoundEndpoint(mini_server_->bound_rpc_addr()),
                        proxy_cache_.get(),
                        &proxy_, &admin_proxy_, &consensus_proxy_, &generic_proxy_, &backup_proxy_);

  WriteRequestPB req;
  req.set_tablet_id(kTabletId);

  WriteResponsePB resp;
  rpc::RpcController controller;

  // We're expecting the write to fail.
  ASSERT_OK(DCHECK_NOTNULL(proxy_.get())->Write(req, &resp, &controller));
  ASSERT_EQ(TabletServerErrorPB::TABLET_NOT_RUNNING, resp.error().code());
  ASSERT_STR_CONTAINS(
      resp.error().status().message(), Format("Tablet $0 not RUNNING: FAILED", kTabletId));
}

TEST_F(TabletServerTest, TestCreateTablet_TabletExists) {
  CreateTabletRequestPB req;
  CreateTabletResponsePB resp;
  RpcController rpc;

  req.set_dest_uuid(mini_server_->server()->fs_manager()->uuid());
  req.set_table_id("testtb");
  req.set_tablet_id(kTabletId);
  req.set_table_name("testtb");
  req.mutable_config()->CopyFrom(mini_server_->CreateLocalConfig());

  Schema schema = SchemaBuilder(schema_).Build();
  SchemaToPB(schema, req.mutable_schema());

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
  // Verify that the tablet exists
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

  // Put some data in the tablet. We flush and insert more rows to ensure that
  // there is data both in the MRS and on disk.
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 1, 1));
  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));
  ASSERT_NO_FATALS(InsertTestRowsRemote(0, 2, 1));

  // Drop any local references to the tablet from within this test,
  // so that when we delete it on the server, it's not held alive
  // by the test code.
  tablet_peer_.reset();

  ASSERT_OK(CallDeleteTablet(mini_server_->server()->fs_manager()->uuid(),
                             kTabletId,
                             tablet::TABLET_DATA_DELETED));

  // Verify that the tablet is removed from the tablet map
  ASSERT_TRUE(ResultToStatus(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId)).IsNotFound());

  // Verify that fetching metrics doesn't crash. Regression test for KUDU-638.
  EasyCurl c;
  faststring buf;
  ASSERT_OK(c.FetchURL(strings::Substitute("http://$0/jsonmetricz",
                                           AsString(mini_server_->bound_http_addr())),
                                           &buf));

  // Verify that after restarting the TS, the tablet is still not in the tablet manager.
  // This ensures that the on-disk metadata got removed.
  Status s = ShutdownAndRebuildTablet();
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
  ASSERT_TRUE(ResultToStatus(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId)).IsNotFound());

  // Verify that the BlockBasedTable mem tracker is still attached to the server mem tracker.
  ASSERT_TRUE(mini_server_->server()->mem_tracker()->FindChild("BlockBasedTable") != nullptr);
}

TEST_F(TabletServerTest, TestDeleteTablet_TabletNotCreated) {
  Status s = CallDeleteTablet(mini_server_->server()->fs_manager()->uuid(),
                              "NotPresentTabletId",
                              tablet::TABLET_DATA_DELETED);
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
}

// Test that with concurrent requests to delete the same tablet, one wins and
// the other fails, with no assertion failures. Regression test for KUDU-345.
TEST_F(TabletServerTest, TestConcurrentDeleteTablet) {
  // Verify that the tablet exists
  ASSERT_OK(mini_server_->server()->tablet_manager()->GetTablet(kTabletId));

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
  ASSERT_TRUE(ResultToStatus(
      mini_server_->server()->tablet_manager()->GetTablet(kTabletId)).IsNotFound());
  ASSERT_EQ(1, num_success);
}

TEST_F(TabletServerTest, TestInsertLatencyMicroBenchmark) {
  METRIC_DEFINE_entity(test);
  METRIC_DEFINE_event_stats(test, insert_latency,
                          "Insert Latency",
                          MetricUnit::kMicroseconds,
                          "TabletServer single threaded insert latency.");

  scoped_refptr<EventStats> stats =
      METRIC_insert_latency.Instantiate(ts_test_metric_entity_);

  auto warmup = AllowSlowTests() ?
      FLAGS_single_threaded_insert_latency_bench_warmup_rows : 10;

  for (int i = 0; i < warmup; i++) {
    InsertTestRowsRemote(0, i, 1);
  }

  auto max_rows = AllowSlowTests() ?
      FLAGS_single_threaded_insert_latency_bench_insert_rows : 100;

  MonoTime start = MonoTime::Now();

  for (int i = warmup; i < warmup + max_rows; i++) {
    MonoTime before = MonoTime::Now();
    InsertTestRowsRemote(0, i, 1);
    MonoTime after = MonoTime::Now();
    MonoDelta delta = after.GetDeltaSince(before);
    stats->Increment(delta.ToMicroseconds());
  }

  MonoTime end = MonoTime::Now();
  double throughput = ((max_rows - warmup) * 1.0) / end.GetDeltaSince(start).ToSeconds();

  // Generate the JSON.
  std::stringstream out;
  JsonWriter writer(&out, JsonWriter::PRETTY);
  ASSERT_OK(stats->WriteAsJson(&writer, MetricJsonOptions()));

  LOG(INFO) << "Throughput: " << throughput << " rows/sec.";
  LOG(INFO) << out.str();
}

// Simple test to ensure we can destroy an RpcServer in different states of
// initialization before Start()ing it.
TEST_F(TabletServerTest, TestRpcServerCreateDestroy) {
  server::RpcServerOptions opts;
  {
    server::RpcServer server1(
        "server1", opts, rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>());
  }
  {
    MessengerBuilder mb("foo");
    auto messenger = rpc::CreateAutoShutdownMessengerHolder(ASSERT_RESULT(mb.Build()));
    {
      server::RpcServer server2(
          "server2", opts, rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>());
      ASSERT_OK(server2.Init(messenger.get()));
    }
  }
}

// Simple test to ensure we can create RpcServer with different bind address options.
TEST_F(TabletServerTest, TestRpcServerRPCFlag) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_bind_addresses) = "0.0.0.0:2000";
  server::RpcServerOptions opts;
  ServerRegistrationPB reg;
  auto tbo = ASSERT_RESULT(TabletServerOptions::CreateTabletServerOptions());
  MessengerBuilder mb("foo");
  auto messenger = CreateAutoShutdownMessengerHolder(ASSERT_RESULT(mb.Build()));

  server::RpcServer server1(
      "server1", opts, rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>());
  ASSERT_OK(server1.Init(messenger.get()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_bind_addresses) = "0.0.0.0:2000,0.0.0.1:2001";
  server::RpcServerOptions opts2;
  server::RpcServer server2(
      "server2", opts2, rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>());
  ASSERT_OK(server2.Init(messenger.get()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_bind_addresses) = "10.20.30.40:2017";
  server::RpcServerOptions opts3;
  server::RpcServer server3(
      "server3", opts3, rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>());
  ASSERT_OK(server3.Init(messenger.get()));

  reg.Clear();
  tbo.fs_opts.data_paths = { GetTestPath("fake-ts") };
  tbo.rpc_opts = opts3;
  TabletServer server(tbo);

  ASSERT_NO_FATALS(WARN_NOT_OK(server.Init(), "Ignore"));
  // This call will fail for http binding, but this test is for rpc.
  ASSERT_NO_FATALS(WARN_NOT_OK(server.GetRegistration(&reg), "Ignore"));
  ASSERT_EQ(1, reg.private_rpc_addresses().size());
  ASSERT_EQ("10.20.30.40", reg.private_rpc_addresses(0).host());
  ASSERT_EQ(2017, reg.private_rpc_addresses(0).port());

  reg.Clear();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_bind_addresses) = "10.20.30.40:2017,20.30.40.50:2018";
  server::RpcServerOptions opts4;
  tbo.rpc_opts = opts4;
  TabletServer tserver2(tbo);
  ASSERT_NO_FATALS(WARN_NOT_OK(tserver2.Init(), "Ignore"));
  // This call will fail for http binding, but this test is for rpc.
  ASSERT_NO_FATALS(WARN_NOT_OK(tserver2.GetRegistration(&reg), "Ignore"));
  ASSERT_EQ(2, reg.private_rpc_addresses().size());
}

// We are not checking if a row is out of bounds in YB because we are using a completely different
// hash-based partitioning scheme, so this test is now actually testing that there is no error
// returned from the server. If we introduce such range checking, this test could be enhanced to
// test for it.
TEST_F(TabletServerTest, TestWriteOutOfBounds) {
  const char *tabletId = "TestWriteOutOfBoundsTablet";
  Schema schema = SchemaBuilder(schema_).Build();

  dockv::PartitionSchema partition_schema;
  CHECK_OK(dockv::PartitionSchema::FromPB(PartitionSchemaPB(), schema, &partition_schema));

  dockv::Partition partition;
  auto table_info = std::make_shared<tablet::TableInfo>(
      "TEST: ", tablet::Primary::kTrue, "TestWriteOutOfBoundsTable", "test_ns", tabletId,
      YQL_TABLE_TYPE, schema, qlexpr::IndexMap(), std::nullopt /* index_info */,
      0 /* schema_version */, partition_schema, "" /* pg_table_id */,
      tablet::SkipTableTombstoneCheck::kFalse);
  ASSERT_OK(mini_server_->server()->tablet_manager()->CreateNewTablet(
      table_info, tabletId, partition, mini_server_->CreateLocalConfig()));

  ASSERT_OK(WaitForTabletRunning(tabletId));

  WriteRequestPB req;
  WriteResponsePB resp;
  RpcController controller;
  req.set_tablet_id(tabletId);

  for (auto op : { QLWriteRequestPB::QL_STMT_INSERT, QLWriteRequestPB::QL_STMT_UPDATE }) {
    AddTestRow(20, 1, "1", op, &req);
    ASSERT_OK(proxy_->Write(req, &resp, &controller));
    SCOPED_TRACE(resp.DebugString());
    ASSERT_FALSE(resp.has_error());
    req.clear_ql_write_batch();
    controller.Reset();
  }
}

namespace {

void CalcTestRowChecksum(uint64_t *out, int32_t key, uint8_t string_field_defined = true) {
  QLValue value;

  string strval = strings::Substitute("original$0", key);
  string buffer;
  uint32_t index = 0;
  buffer.append(pointer_cast<const char*>(&index), sizeof(index));
  value.set_int32_value(key);
  value.value().AppendToString(&buffer);

  index = 1;
  buffer.append(pointer_cast<const char*>(&index), sizeof(index));
  value.value().AppendToString(&buffer);

  index = 2;
  buffer.append(pointer_cast<const char*>(&index), sizeof(index));
  buffer.append(pointer_cast<const char*>(&string_field_defined), sizeof(string_field_defined));
  if (string_field_defined) {
    value.set_string_value(strval);
    value.value().AppendToString(&buffer);
  }

  crc::Crc* crc = crc::GetCrc32cInstance();
  crc->Compute(buffer.c_str(), buffer.size(), out, nullptr);
}

} // namespace

// Simple test to check that our checksum scans work as expected.
TEST_F(TabletServerTest, TestChecksumScan) {
  uint64_t total_crc = 0;

  ChecksumRequestPB req;
  req.set_tablet_id(kTabletId);
  ChecksumResponsePB resp;
  RpcController controller;
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));

  // No rows.
  ASSERT_EQ(total_crc, resp.checksum());

  // First row.
  int32_t key = 1;
  InsertTestRowsRemote(0, key, 1);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  CalcTestRowChecksum(&total_crc, key);
  uint64_t first_crc = total_crc; // Cache first record checksum.

  ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
  ASSERT_EQ(total_crc, resp.checksum());

  // Second row (null string field).
  key = 2;
  InsertTestRowsRemote(0, key, 1, 1, nullptr, kTabletId, nullptr, nullptr, false);
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  CalcTestRowChecksum(&total_crc, key, false);

  ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
  ASSERT_EQ(total_crc, resp.checksum());

  // Finally, delete row 2, so we're back to the row 1 checksum.
  ASSERT_NO_FATALS(DeleteTestRowsRemote(key, 1));
  controller.Reset();
  ASSERT_OK(proxy_->Checksum(req, &resp, &controller));
  ASSERT_NE(total_crc, resp.checksum());
  ASSERT_EQ(first_crc, resp.checksum());
}

TEST_F(TabletServerTest, TestCallHome) {
  const auto webserver_dir = GetWebserverDir();
  CHECK_OK(env_->CreateDir(webserver_dir));
  TestCallHome<TabletServer, TserverCallHome>(
      webserver_dir, {} /*additional_collections*/, mini_server_->server());
}

// This tests whether the enabling/disabling of callhome is happening dynamically
// during runtime.
TEST_F(TabletServerTest, TestCallHomeFlag) {
  const auto webserver_dir = GetWebserverDir();
  CHECK_OK(env_->CreateDir(webserver_dir));
  TestCallHomeFlag<TabletServer, TserverCallHome>(webserver_dir, mini_server_->server());
}

TEST_F(TabletServerTest, TestGFlagsCallHome) {
  CHECK_OK(env_->CreateDir(GetWebserverDir()));
  TestGFlagsCallHome<TabletServer, TserverCallHome>(mini_server_->server());
}

#if YB_TCMALLOC_ENABLED
TEST_F(TabletServerTest, TestUntrackedMemory) {
  auto server_metric_entity = mini_server_->server()->metric_entity();
  ASSERT_TRUE(server_metric_entity->TEST_ContainsMetricName("untracked_memory"));

  const size_t kBufferSize = 10_MB;
  std::unique_ptr<char[]> alloc(new char[kBufferSize]);
  // Clang in release mode can optimize out the above allocation unless
  // we do something with the pointer... so we just log it.
  VLOG(8) << static_cast<void*>(alloc.get());
  ASSERT_LE(kBufferSize, server_metric_entity->FindOrNull<FunctionGauge<uint64_t>>(
      METRIC_untracked_memory)->value());
}
#endif

} // namespace tserver
} // namespace yb
