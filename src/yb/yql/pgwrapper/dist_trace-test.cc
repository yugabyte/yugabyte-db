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

#include <algorithm>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

#include "yb/server/webserver.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

namespace yb::pgwrapper {

namespace {

namespace otlp = opentelemetry::proto::collector::trace::v1;
namespace otlp_common = opentelemetry::proto::common::v1;
namespace otlp_resource = opentelemetry::proto::resource::v1;

std::string FindStringAttribute(
    const google::protobuf::RepeatedPtrField<otlp_common::KeyValue>& attributes,
    const std::string& key) {
  for (const auto& attr : attributes) {
    if (attr.key() == key && attr.value().has_string_value()) {
      return attr.value().string_value();
    }
  }
  return {};
}

int64_t FindIntAttribute(
    const google::protobuf::RepeatedPtrField<otlp_common::KeyValue>& attributes,
    const std::string& key) {
  for (const auto& attr : attributes) {
    if (attr.key() == key && attr.value().has_int_value()) {
      return attr.value().int_value();
    }
  }
  return 0;
}

struct TraceparentInfo {
  std::string full;
  std::string trace_id;
};

std::string RandomHexString(int num_bytes) {
  std::string result;
  result.reserve(num_bytes * 2);
  for (int i = 0; i < num_bytes; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", RandomUniformInt<int>(0, 255));
    result.append(buf);
  }
  return result;
}

TraceparentInfo GenerateTraceparent() {
  auto trace_id = RandomHexString(16);
  auto parent_id = RandomHexString(8);
  return {
      .full = Format("00-$0-$1-01", trace_id, parent_id),
      .trace_id = trace_id,
  };
}

YB_DEFINE_ENUM(QueryExecMode, (kFetch)(kExecute));

struct TestQuery {
  QueryExecMode mode;
  std::string sql;
};

class OtlpHttpCollector {
 public:
  struct SpanRecord {
    std::string service_name;
    std::string op_name;
    std::string query_text;
    std::string trace_id;
    int64_t db_id = 0;
    int64_t user_id = 0;

    bool operator==(const SpanRecord& other) const {
      bool trace_id_match = trace_id.empty() || other.trace_id.empty() ||
                            trace_id == other.trace_id;
      return service_name == other.service_name && op_name == other.op_name &&
             query_text == other.query_text && db_id == other.db_id &&
             user_id == other.user_id && trace_id_match;
    }

    friend std::ostream& operator<<(std::ostream& os, const SpanRecord& s) {
      return os << "{service=" << s.service_name << " op=" << s.op_name
                << " query=" << s.query_text << " db_id=" << s.db_id
                << " user_id=" << s.user_id << " trace_id=" << s.trace_id << "}";
    }
  };

  Status Start() {
    WebserverOptions opts;
    opts.port = 0;
    opts.bind_interface = "0.0.0.0";
    server_ = std::make_unique<Webserver>(opts, "DistTraceTestCollector");
    server_->RegisterPathHandler(
        "/v1/traces", "OTLP traces",
        [this](const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
          HandleTraceRequest(req, resp);
        });
    RETURN_NOT_OK(server_->Start());

    std::vector<Endpoint> addrs;
    RETURN_NOT_OK(server_->GetBoundAddresses(&addrs));
    if (addrs.size() != 1) {
      return STATUS_FORMAT(
          IllegalState, "Expected a single bound OTLP collector address, got $0", addrs.size());
    }
    endpoint_ = yb::ToString(addrs.front());
    return Status::OK();
  }

  std::string Url() const {
    return Format("http://$0/v1/traces", endpoint_);
  }

  std::vector<SpanRecord> GetRootSpansForTestQueries(
      const std::vector<std::string>& queries) const {
    std::vector<SpanRecord> root_spans;
    auto all_spans = GetSpans();

    for (const auto& query : queries) {
      for (const auto& span : all_spans) {
        // Only include root spans
        if (span.op_name != "ysql.query") {
          continue;
        }

        if (span.query_text == query) {
          root_spans.push_back(span);
        }
      }
    }

    return root_spans;
  }

  std::vector<SpanRecord> GetSpans() const EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    return spans_;
  }

 private:
  void HandleTraceRequest(const Webserver::WebRequest& req, Webserver::WebResponse* resp) {
    if (req.request_method != "POST") {
      resp->code = 405;
      return;
    }

    otlp::ExportTraceServiceRequest request;
    if (!request.ParseFromString(req.post_data)) {
      resp->code = 400;
      return;
    }

    RecordSpans(request);
    resp->code = 200;
  }

  void RecordSpans(const otlp::ExportTraceServiceRequest& request) {
    std::lock_guard lock(mutex_);
    for (const auto& resource_spans : request.resource_spans()) {
      const auto service_name =
          FindStringAttribute(resource_spans.resource().attributes(), "service.name");
      for (const auto& traces : resource_spans.scope_spans()) {
        for (const auto& span : traces.spans()) {
          spans_.push_back(SpanRecord{
              .service_name = service_name,
              .op_name = span.name(),
              .query_text = FindStringAttribute(span.attributes(), "query.text"),
              .trace_id = FindStringAttribute(span.attributes(), "trace.id"),
              .db_id = FindIntAttribute(span.attributes(), "db.id"),
              .user_id = FindIntAttribute(span.attributes(), "user.id"),
          });
        }
      }
    }
  }

  mutable std::mutex mutex_;
  std::vector<SpanRecord> spans_ GUARDED_BY(mutex_);
  std::string endpoint_;
  std::unique_ptr<Webserver> server_;
};

class DistTraceTest : public LibPqTestBase {
 public:
  virtual ~DistTraceTest() = default;

  void SetUp() override {
    LibPqTestBase::SetUp();
    conn_ = ASSERT_RESULT(Connect(true /* simple_query_protocol */));
    std::tie(db_oid_, user_oid_) = ASSERT_RESULT(FetchDbAndUserOid());
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    ASSERT_OK(receiver_.Start());
    ConfigureClusterOptions(options);
    ConfigureDistTraceOptions(options);
  }

 protected:
  void ConfigureClusterOptions(ExternalMiniClusterOptions* options) {
    options->replication_factor = 1;
  }

  void ConfigureDistTraceOptions(ExternalMiniClusterOptions* options) {
    AppendFlagToAllowedPreviewFlagsCsv(options->extra_tserver_flags,
        "otel_collector_traces_endpoint");
    options->extra_tserver_flags.push_back(
        Format("--otel_collector_traces_endpoint=$0", receiver_.Url()));
    options->extra_tserver_flags.push_back(
        Format("--otel_batch_schedule_delay_ms=$0", kOtelBatchScheduleDelayMs));
    options->extra_tserver_flags.push_back(
        Format("--otel_batch_max_export_batch_size=$0", kOtelBatchMaxExportBatchSize));
    options->extra_tserver_flags.push_back(
        Format("--otel_batch_max_queue_size=$0", kOtelBatchMaxQueueSize));
  }

  int GetNumTabletServers() const override {
    return 1;
  }

  static std::vector<TestQuery> GetTestQueries(const std::string& table) {
    return {
        {QueryExecMode::kExecute, Format("create table $0 (id int, val text)", table)},
        {QueryExecMode::kExecute, Format("insert into $0 values (1, 'hello')", table)},
        {QueryExecMode::kFetch, Format("select * from $0", table)},
        {QueryExecMode::kExecute, Format("update $0 set val = 'world' where id = 1", table)},
        {QueryExecMode::kExecute, Format("delete from $0 where id = 1", table)},
        {QueryExecMode::kExecute, Format("alter table $0 add column extra int", table)},
    };
  }

  Status ExecuteQuery(const std::string& query, QueryExecMode mode) {
    if (mode == QueryExecMode::kFetch) {
      RETURN_NOT_OK(conn_->Fetch(query));
    } else {
      RETURN_NOT_OK(conn_->Execute(query));
    }
    return Status::OK();
  }

  Result<std::pair<int64_t, int64_t>> FetchDbAndUserOid() {
    // TODO (#30816): FetchRow<T> for non-string types fails on simple query protocol connections
    auto db_oid_result = VERIFY_RESULT(
        conn_->Fetch("SELECT oid FROM pg_database WHERE datname = current_database()"));
    auto db_oid = static_cast<int64_t>(std::stoul(PQgetvalue(db_oid_result.get(), 0, 0)));

    // TODO (#30816): FetchRow<T> for non-string types fails on simple query protocol connections
    auto user_oid_result =
        VERIFY_RESULT(conn_->Fetch("SELECT oid FROM pg_authid WHERE rolname = current_user"));
    auto user_oid = static_cast<int64_t>(std::stoul(PQgetvalue(user_oid_result.get(), 0, 0)));

    return std::make_pair(db_oid, user_oid);
  }

  OtlpHttpCollector::SpanRecord MakeExpected(
      const std::string& query, const std::string& trace_id = {}) {
    return OtlpHttpCollector::SpanRecord{
        .service_name = "ysql",
        .op_name = "ysql.query",
        .query_text = query,
        .trace_id = trace_id,
        .db_id = db_oid_,
        .user_id = user_oid_,
    };
  }

  Status VerifySpans(const std::vector<OtlpHttpCollector::SpanRecord>& expected) {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);

    std::vector<std::string> queries;
    for (const auto& span : expected) {
      queries.push_back(span.query_text);
    }

    SCHECK_EQ(expected, receiver_.GetRootSpansForTestQueries(queries), IllegalState,
              "Spans mismatch");
    return Status::OK();
  }

  Status VerifyNoSpansEmitted() {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);
    auto spans = receiver_.GetSpans();
    SCHECK(spans.empty(), IllegalState,
        Format("Expected no spans, but found $0", spans.size()));
    return Status::OK();
  }

  Status VerifyQueryNotTraced(const std::string& query) {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);
    auto spans = receiver_.GetRootSpansForTestQueries({query});
    SCHECK(spans.empty(), IllegalState,
        Format("Expected no traced spans for query '$0', but found $1",
               query, spans.size()));
    return Status::OK();
  }

  static constexpr auto kTraceparentValueTooShort = "11111111";
  static constexpr auto kTraceparentValueTooLong =
      "111111111111111111111111111111111111111111111111111111111111";

  inline static const std::vector<std::string> kInvalidTraceparentValues = {
      "00-00000000000000000000000000000000-0000000000000005-01", // All zeros trace ID
      "00-00000000000000000000000000000009-0000000000000000-01", // All zeros parent ID
      "00-ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ-0000000000000005-01", // Invalid hex for trace ID
      "00-00000000000000000000000000000009-ZZZZZZZZZZZZZZZZ-01", // Invalid hex for parent ID
  };

  static constexpr auto kOtelBatchMaxQueueSize = 25;
  static constexpr auto kOtelBatchMaxExportBatchSize = 10;
  static constexpr auto kOtelBatchScheduleDelayMs = 100;

  std::vector<std::string>& CaptureWarnings() {
    warnings_.clear();
    conn_->SetNoticeProcessor(
        [](void* arg, const char* message) {
          static_cast<std::vector<std::string>*>(arg)->emplace_back(message);
        },
        &warnings_);
    return warnings_;
  }

  std::optional<pgwrapper::PGConn> conn_;
  OtlpHttpCollector receiver_;
  int64_t db_oid_ = 0;
  int64_t user_oid_ = 0;
  std::vector<std::string> warnings_;
};

class DistTraceDisabledTest : public DistTraceTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    ConfigureClusterOptions(options);
  }

  void ConfigureClusterOptions(ExternalMiniClusterOptions* options) {
    options->replication_factor = 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }
};

}  // namespace

TEST_F(DistTraceTest, TestTraceparentComment) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;

  for (const auto& [mode, query] : GetTestQueries("test_comment")) {
    auto tp = GenerateTraceparent();
    auto traced_query = Format("/*traceparent='$0'*/ $1;", tp.full, query);
    ASSERT_OK(ExecuteQuery(traced_query, mode));
    expected.push_back(MakeExpected(traced_query, tp.trace_id));
  }

  ASSERT_OK(VerifySpans(expected));
}

TEST_F(DistTraceTest, TestTraceparentGuc) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;

  for (const auto& [mode, query] : GetTestQueries("test_guc")) {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

    ASSERT_OK(ExecuteQuery(query, mode));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  ASSERT_OK(VerifySpans(expected));
}

TEST_F(DistTraceTest, TestTraceparentGucSetLocal) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;
  auto tp = GenerateTraceparent();

  ASSERT_OK(conn_->Execute("BEGIN"));

  ASSERT_OK(conn_->ExecuteFormat("SET LOCAL yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  ASSERT_OK(conn_->Fetch("SELECT 1"));
  expected.push_back(MakeExpected("SELECT 1", tp.trace_id));

  ASSERT_OK(conn_->Execute("COMMIT"));
  expected.push_back(MakeExpected("COMMIT", tp.trace_id));

  ASSERT_OK(VerifySpans(expected));

  // After COMMIT, SET LOCAL is reverted so the traceparent should be cleared.
  ASSERT_OK(conn_->Fetch("SELECT 2"));
  ASSERT_OK(VerifyQueryNotTraced("SELECT 2"));
}

TEST_F(DistTraceTest, TestMalformedTraceparentGuc) {
  ASSERT_NOK_STR_CONTAINS(conn_->Execute("SET yb_dist_tracecontext = 'random_garbage'"),
      "no traceparent field found");

  auto tp = GenerateTraceparent();
  ASSERT_NOK_STR_CONTAINS(
      conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=$0''X'", tp.full),
          "traceparent value missing opening quote");

  ASSERT_NOK_STR_CONTAINS(
      conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0X'", tp.full),
          "traceparent value missing closing quote");

  for (const auto& tp_value : kInvalidTraceparentValues) {
    ASSERT_NOK_STR_CONTAINS(
        conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp_value),
            "traceparent format is invalid");
  }

  for (const auto& tp_value : {kTraceparentValueTooShort, kTraceparentValueTooLong}) {
    ASSERT_NOK_STR_CONTAINS(
        conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp_value),
        "traceparent field doesn't have the correct size");
  }

  // No spans should be recorded for the malformed traceparent values.
  ASSERT_OK(VerifyNoSpansEmitted());
}

TEST_F(DistTraceTest, TestMalformedTraceparentComment) {
  // Queries with malformed traceparent comments should succeed but produce no spans.
  // Comments that fail extraction emit a WARNING (not an error) to the client.
  // The padding ensures query_len >= YB_TRACEPARENT_COMMENT_MIN_LEN (73).
  const std::string kPadding =
      " SELECT 'padding_to_make_query_long_enough_for_min_length_check';";

  const auto& warnings = CaptureWarnings();
  int num_warnings = 0;

  ASSERT_OK(conn_->FetchFormat(
      "/*a_regular_comment_without_any_trace_context_data_present_in_here*/$0", kPadding));

  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->FetchFormat("/*traceparent=$0'XX*/$1", tp.full, kPadding));
  ASSERT_EQ(warnings.size(), ++num_warnings);
  ASSERT_STR_CONTAINS(warnings.back(), "traceparent value missing opening quote");

  ASSERT_OK(conn_->FetchFormat("/*traceparent='$0XX*/$1", tp.full, kPadding));
  ASSERT_EQ(warnings.size(), ++num_warnings);
  ASSERT_STR_CONTAINS(warnings.back(), "traceparent value missing closing quote");

  for (const auto& tp_value : kInvalidTraceparentValues) {
    ASSERT_OK(conn_->FetchFormat("/*traceparent='$0'*/ SELECT 1;", tp_value));
    ASSERT_EQ(warnings.size(), ++num_warnings);
    ASSERT_STR_CONTAINS(warnings.back(), "traceparent format is invalid");
  }

  for (const auto& tp_value : {kTraceparentValueTooShort, kTraceparentValueTooLong}) {
    ASSERT_OK(conn_->FetchFormat("/*traceparent='$0'*/ SELECT 1;", tp_value));
    ASSERT_EQ(warnings.size(), ++num_warnings);
    ASSERT_STR_CONTAINS(warnings.back(), "traceparent field doesn't have the correct size");
  }

  ASSERT_OK(VerifyNoSpansEmitted());
}

TEST_F(DistTraceTest, TestValidEdgeCasesComment) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;

  // Leading whitespace before the comment.
  {
    auto tp = GenerateTraceparent();
    auto query = Format("   /*traceparent='$0'*/ SELECT 1;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  // Extra content in the comment before and after traceparent.
  {
    auto tp = GenerateTraceparent();
    auto query = Format(
        "/*some_extra_stuff traceparent='$0' more_stuff_here*/ SELECT 2;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  // Tab and newline whitespace before the comment.
  {
    auto tp = GenerateTraceparent();
    auto query = Format("\t\n /*traceparent='$0'*/ SELECT 3;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  ASSERT_OK(VerifySpans(expected));
}

TEST_F(DistTraceTest, TestValidEdgeCasesGuc) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;

  // Whitespace before traceparent= (strstr still finds the key).
  {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = '   traceparent=''$0'''", tp.full));

    auto query = "SELECT 1;";
    ASSERT_OK(conn_->Fetch(query));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  // Extra content before and after traceparent= (strstr still finds the key).
  {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat(
        "SET yb_dist_tracecontext = 'extra_before traceparent=''$0'' extra_after'", tp.full));

    auto query = "SELECT 2;";
    ASSERT_OK(conn_->Fetch(query));
    expected.push_back(MakeExpected(query, tp.trace_id));
  }

  ASSERT_OK(VerifySpans(expected));
}

TEST_F(DistTraceTest, TestTraceparentGucSetPersistsAcrossCommit) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;
  auto tp = GenerateTraceparent();

  ASSERT_OK(conn_->Execute("BEGIN"));

  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  ASSERT_OK(conn_->Fetch("SELECT 1"));
  expected.push_back(MakeExpected("SELECT 1", tp.trace_id));

  ASSERT_OK(conn_->Execute("COMMIT"));
  expected.push_back(MakeExpected("COMMIT", tp.trace_id));

  // After COMMIT, plain SET (not SET LOCAL) should persist.
  ASSERT_OK(conn_->Fetch("SELECT 2"));
  expected.push_back(MakeExpected("SELECT 2", tp.trace_id));

  ASSERT_OK(VerifySpans(expected));

  // After RESET, traceparent is cleared and queries should not be traced.
  ASSERT_OK(conn_->Execute("RESET yb_dist_tracecontext"));

  ASSERT_OK(conn_->Fetch("SELECT 3"));
  ASSERT_OK(VerifyQueryNotTraced("SELECT 3"));
}

TEST_F(DistTraceTest, TestFailedGucSetKeepsPreviousValue) {
  std::vector<OtlpHttpCollector::SpanRecord> expected;

  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  // Malformed SETs, all should fail, leaving the valid GUC intact.
  ASSERT_NOK(conn_->Execute("SET yb_dist_tracecontext = 'random_garbage'"));
  ASSERT_NOK(conn_->Execute("SET yb_dist_tracecontext = 'traceparent=''too-short'''"));
  for (const auto& tp_value : kInvalidTraceparentValues) {
    ASSERT_NOK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp_value));
  }

  auto query = "SELECT 1;";
  ASSERT_OK(conn_->Fetch(query));
  expected.push_back(MakeExpected(query, tp.trace_id));

  ASSERT_OK(VerifySpans(expected));
}

TEST_F(DistTraceDisabledTest, TestTraceparentWhenDistTraceDisabled) {
  auto tp = GenerateTraceparent();

  // GUC SET should fail because dist tracing is not enabled.
  ASSERT_NOK_STR_CONTAINS(
      conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full),
      "distributed tracing is not enabled. Set otel_collector_traces_endpoint flag to enable "
      "distributed tracing.");

  // Comment traceparent should be silently ignored - no warning, no error.
  const auto& warnings = CaptureWarnings();

  ASSERT_OK(conn_->FetchFormat("/*traceparent='$0'*/ SELECT 1;", tp.full));
  ASSERT_TRUE(warnings.empty());
}

}  // namespace yb::pgwrapper
