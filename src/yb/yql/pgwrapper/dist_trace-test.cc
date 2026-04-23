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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

#include "yb/server/webserver.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

namespace yb::pgwrapper {

namespace {

namespace otlp = opentelemetry::proto::collector::trace::v1;
namespace otlp_common = opentelemetry::proto::common::v1;
namespace otlp_resource = opentelemetry::proto::resource::v1;

static constexpr auto kOtelBatchMaxQueueSize = 4096;
static constexpr auto kOtelBatchMaxExportBatchSize = 512;
static constexpr auto kOtelBatchScheduleDelayMs = 100;

YB_DEFINE_ENUM(QueryExecMode, (kFetch)(kExecute));
YB_STRONGLY_TYPED_BOOL(IsUtility);
YB_DEFINE_ENUM(SpanType,
    (kRoot)(kParse)(kRewrite)(kExecute)(kPlan)(kCommit)(kAbort)
    (kExtParse)(kExtBind)(kExtExecute)(kExtSync)(kExtDescribe)(kExtFlush));

struct ExpectedSpan {
  SpanType type;
  size_t count;
};

struct TraceparentInfo {
  std::string full;
  std::string trace_id;
};

struct TestQuery {
  QueryExecMode mode;
  IsUtility is_utility;
  std::string sql;
  std::vector<ExpectedSpan> expected_spans;
};

struct Span {
  std::string service_name;
  std::string op_name;
  std::string query_text;
  std::string trace_id;
  std::string parent_span_id;
  std::string span_id;
  int64_t db_id = 0;
  int64_t user_id = 0;

  bool operator<(const Span& other) const {
    return std::tie(op_name, service_name, query_text, trace_id, db_id, user_id) <
           std::tie(other.op_name, other.service_name, other.query_text,
                    other.trace_id, other.db_id, other.user_id);
  }
};

struct Trace {
  std::string trace_id;
  std::string query_text;
  std::vector<Span> spans;
};

// trace_id -> Trace
using TraceMap = std::unordered_map<std::string, Trace>;

std::string FindStringAttribute(
    const google::protobuf::RepeatedPtrField<otlp_common::KeyValue>& attributes,
    std::string_view key) {
  for (const auto& attr : attributes) {
    if (attr.key() == key && attr.value().has_string_value()) {
      return attr.value().string_value();
    }
  }
  return {};
}

int64_t FindIntAttribute(
    const google::protobuf::RepeatedPtrField<otlp_common::KeyValue>& attributes,
    std::string_view key) {
  for (const auto& attr : attributes) {
    if (attr.key() == key && attr.value().has_int_value()) {
      return attr.value().int_value();
    }
  }
  return 0;
}

using strings::b2a_hex;

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

class OtlpHttpCollector {
  // All executor node span names produced by YbGetExecNodeSpanName() in execProcnode.c.
  // These have variable counts depending on row cardinality and are filtered out during
  // span verification; they are tested separately via HasSpanWithName.
  static inline const std::unordered_set<std::string_view> kExecutorNodeSpanNames = {
      "Result", "ProjectSet", "Insert", "Update", "Delete", "Merge",
      "Append", "Merge Append", "Recursive Union",
      "Seq Scan", "Sample Scan", "Index Scan", "Index Only Scan",
      "Bitmap Heap Scan", "YB Bitmap Table Scan",
      "Tid Scan", "Tid Range Scan", "Subquery Scan", "Function Scan",
      "Table Function Scan", "Values Scan", "CTE Scan", "Named Tuplestore Scan",
      "WorkTable Scan", "YB Foreign Scan", "Foreign Scan", "Foreign Insert",
      "Foreign Update", "Foreign Delete", "Custom Scan",
      "Nested Loop", "YB Batched Nested Loop", "Merge Join", "Hash Join",
      "Materialize", "Memoize", "Sort", "Incremental Sort", "Group",
      "Aggregate", "WindowAgg", "Unique", "Gather", "Gather Merge",
      "SetOp", "LockRows", "Limit",
  };

 public:
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

  Status VerifySpansMatch(const Trace& actual, const Trace& expected) const {
    // Filter out per-tuple executor node spans before comparison. These have
    // variable counts depending on row cardinality and are verified separately
    // by HasSpanWithName in the executor node tests.
    std::vector<Span> filtered_actual;
    std::copy_if(actual.spans.begin(), actual.spans.end(),
                 std::back_inserter(filtered_actual),
                 [](const Span& s) { return !kExecutorNodeSpanNames.contains(s.op_name); });

    SCHECK_EQ(filtered_actual.size(), expected.spans.size(), IllegalState,
        Format("Span count mismatch for query '$0': expected $1, got $2",
               expected.query_text, expected.spans.size(), filtered_actual.size()));

    auto get_sorted_op_names = [](const std::vector<Span>& spans) {
      std::vector<std::string> names;
      names.reserve(spans.size());
      for (const auto& s : spans) {
        names.push_back(s.op_name);
      }
      std::sort(names.begin(), names.end());
      return names;
    };

    auto actual_names = get_sorted_op_names(filtered_actual);
    auto expected_names = get_sorted_op_names(expected.spans);

    for (size_t i = 0; i < actual_names.size(); ++i) {
      SCHECK_EQ(actual_names[i], expected_names[i], IllegalState,
          Format("Span mismatch at index $0 for query '$1': expected '$2', got '$3'",
                 i, expected.query_text, expected_names[i], actual_names[i]));
    }
    return Status::OK();
  }

  Status VerifyAgainstCollectorTraces(const std::vector<Trace>& expected_traces)
      EXCLUDES(mutex_) {
    RETURN_NOT_OK(WaitFor(
        [this, &expected_traces]() -> Result<bool> {
          std::lock_guard lock(mutex_);
          for (const auto& expected : expected_traces) {
            auto it = traces_.find(expected.trace_id);
            if (it == traces_.end()) return false;
            auto non_exec_count = std::count_if(
                it->second.spans.begin(), it->second.spans.end(),
                [](const Span& s) { return !kExecutorNodeSpanNames.contains(s.op_name); });
            if (non_exec_count < static_cast<int64_t>(expected.spans.size())) {
              return false;
            }
          }
          return true;
        },
        kOtelBatchScheduleDelayMs * kTimeMultiplier * 50ms,
        "Expected traces to be collected"));

    std::lock_guard lock(mutex_);
    for (const auto& expected : expected_traces) {
      auto it = traces_.find(expected.trace_id);

      SCHECK(it != traces_.end(), IllegalState,
          Format("Trace not found for query '$0'", expected.query_text));

      RETURN_NOT_OK(VerifySpansMatch(it->second, expected));
    }

    return Status::OK();
  }

  void ClearTraces() EXCLUDES(mutex_) {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 5ms);
    std::lock_guard lock(mutex_);
    traces_.clear();
  }

  Status VerifyNoTracesEmitted() const EXCLUDES(mutex_) {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);
    std::lock_guard lock(mutex_);
    SCHECK(traces_.empty(), IllegalState,
        Format("Expected no traces, but found $0", traces_.size()));
    return Status::OK();
  }

  Status VerifyTraceContainsOpName(
      std::string_view trace_id, std::string_view span_op_name) const EXCLUDES(mutex_) {
    return WaitFor(
        [this, trace_id, span_op_name]() -> Result<bool> {
          std::lock_guard lock(mutex_);
          auto it = traces_.find(std::string(trace_id));
          if (it == traces_.end()) {
            return false;
          }
          for (const auto& span : it->second.spans) {
            if (span.op_name == span_op_name) {
              return true;
            }
          }
          return false;
        },
        kOtelBatchScheduleDelayMs * kTimeMultiplier * 30ms,
        Format("Span '$0' to appear in trace '$1'", span_op_name, trace_id));
  }

  bool HasSpanWithName(const std::string& trace_id, std::string_view span_name) const
      EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    auto it = traces_.find(trace_id);
    if (it == traces_.end()) return false;
    for (const auto& span : it->second.spans) {
      if (span.op_name == span_name) return true;
    }
    return false;
  }

  Status VerifyQueryNotTraced(std::string_view query) const EXCLUDES(mutex_) {
    SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);
    std::lock_guard lock(mutex_);
    for (const auto& [_, trace] : traces_) {
      SCHECK_NE(trace.query_text, query, IllegalState,
          Format("Expected no trace for query '$0', but found $1",
                 query, trace.trace_id));
    }
    return Status::OK();
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
          const auto trace_id = b2a_hex(span.trace_id());

          // Insert if new, or get existing; handles spans arriving in multiple batches
          auto& trace = traces_[trace_id];
          if (trace.trace_id.empty()) {
            trace.trace_id = trace_id;
          }

          // Only take query_text from the first query span we see for this trace
          if (trace.query_text.empty() && span.name() == "query") {
            trace.query_text = FindStringAttribute(span.attributes(), "query.text");
          }
          trace.spans.push_back(Span{
              .service_name = service_name,
              .op_name = span.name(),
              .query_text = FindStringAttribute(span.attributes(), "query.text"),
              .trace_id = trace_id,
              .parent_span_id = b2a_hex(span.parent_span_id()),
              .span_id = b2a_hex(span.span_id()),
              .db_id = FindIntAttribute(span.attributes(), "db.id"),
              .user_id = FindIntAttribute(span.attributes(), "user.id"),
          });
        }
      }
    }
  }

  mutable std::mutex mutex_;
  // trace_id -> (query_text, spans)
  TraceMap traces_ GUARDED_BY(mutex_);
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
    ASSERT_OK(collector_.Start());
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
        Format("--otel_collector_traces_endpoint=$0", collector_.Url()));
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

  static std::vector<TestQuery> GetTestQueries(std::string_view table) {
    return {
        {QueryExecMode::kExecute, IsUtility::kTrue,
            Format("create table $0 (id int, val text)", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 2},
             {SpanType::kRewrite, 4},
             {SpanType::kPlan, 3},
             {SpanType::kExecute, 1},
             {SpanType::kCommit, 1}}},
        {QueryExecMode::kExecute, IsUtility::kFalse,
            Format("insert into $0 values (1, 'hello')", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 2},
             {SpanType::kCommit, 1}}},
        {QueryExecMode::kFetch, IsUtility::kFalse,
            Format("select * from $0", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 1},
             {SpanType::kCommit, 1}}},
        {QueryExecMode::kExecute, IsUtility::kFalse,
            Format("update $0 set val = 'world' where id = 1", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 2},
             {SpanType::kCommit, 1}}},
        {QueryExecMode::kExecute, IsUtility::kFalse,
            Format("delete from $0 where id = 1", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 2},
             {SpanType::kCommit, 1}}},
        {QueryExecMode::kExecute, IsUtility::kTrue,
            Format("alter table $0 add column extra int", table),
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 2},
             {SpanType::kRewrite, 3},
             {SpanType::kPlan, 2},
             {SpanType::kExecute, 1},
             {SpanType::kCommit, 1}}},
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

  Span GetSpan(
      std::string_view op_name, std::string_view trace_id,
      std::string_view query_text = {}, int64_t db_id = 0, int64_t user_id = 0) const {
    return Span{
        .service_name = "ysql",
        .op_name = std::string(op_name),
        .query_text = std::string(query_text),
        .trace_id = std::string(trace_id),
        .parent_span_id = {},
        .span_id = {},
        .db_id = db_id,
        .user_id = user_id,
    };
  }

  Trace MakeExpectedTrace(
      std::string_view query, std::string_view trace_id,
      const std::vector<ExpectedSpan>& expected_spans) const {

    Trace trace = {
        .trace_id = std::string(trace_id),
        .query_text = std::string(query),
        .spans = {},
    };

    for (const auto& [span_type, count] : expected_spans) {
      for (size_t i = 0; i < count; ++i) {
        switch (span_type) {
          case SpanType::kRoot:
            trace.spans.push_back(GetSpan("query", trace_id, query, db_oid_, user_oid_));
            break;
          case SpanType::kParse:
            trace.spans.push_back(GetSpan("parse", trace_id));
            break;
          case SpanType::kRewrite:
            trace.spans.push_back(GetSpan("rewrite", trace_id));
            break;
          case SpanType::kExecute:
            trace.spans.push_back(GetSpan("execute", trace_id));
            break;
          case SpanType::kPlan:
            trace.spans.push_back(GetSpan("plan", trace_id));
            break;
          case SpanType::kCommit:
            trace.spans.push_back(GetSpan("commit", trace_id));
            break;
          case SpanType::kAbort:
            trace.spans.push_back(GetSpan("abort", trace_id));
            break;
          case SpanType::kExtParse:
            trace.spans.push_back(GetSpan("ext.parse", trace_id));
            break;
          case SpanType::kExtBind:
            trace.spans.push_back(GetSpan("ext.bind", trace_id));
            break;
          case SpanType::kExtExecute:
            trace.spans.push_back(GetSpan("ext.execute", trace_id));
            break;
          case SpanType::kExtSync:
            trace.spans.push_back(GetSpan("ext.sync", trace_id));
            break;
          case SpanType::kExtDescribe:
            trace.spans.push_back(GetSpan("ext.describe", trace_id));
            break;
          case SpanType::kExtFlush:
            trace.spans.push_back(GetSpan("ext.flush", trace_id));
            break;
        }
      }
    }

    std::sort(trace.spans.begin(), trace.spans.end());
    return trace;
  }

  Status CreateTable(const std::string& table_name, int num_rows = 20) {
    RETURN_NOT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (id int, val text)", table_name));
    RETURN_NOT_OK(conn_->ExecuteFormat(
        "INSERT INTO $0 SELECT g, 'row_' || g FROM generate_series(1,$1) g",
        table_name, num_rows));
    return Status::OK();
  }

  Status VerifyExecNodeSpan(
      const std::string& trigger_query, const std::string& expected_span_name) {
    auto tp = GenerateTraceparent();
    RETURN_NOT_OK(conn_->ExecuteFormat(
        "SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

    RETURN_NOT_OK(conn_->Fetch(trigger_query));

    RETURN_NOT_OK(WaitFor(
        [&]() -> Result<bool> {
          return collector_.HasSpanWithName(tp.trace_id, expected_span_name);
        },
        kOtelBatchScheduleDelayMs * kTimeMultiplier * 30ms,
        Format("Waiting for $0 span for: $1", expected_span_name, trigger_query)));

    RETURN_NOT_OK(conn_->Execute("RESET yb_dist_tracecontext"));
    return Status::OK();
  }

  std::vector<std::string>& CaptureWarnings() {
    warnings_.clear();
    conn_->SetNoticeProcessor(
        [](void* arg, const char* message) {
          static_cast<std::vector<std::string>*>(arg)->emplace_back(message);
        },
        &warnings_);
    return warnings_;
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

  std::optional<pgwrapper::PGConn> conn_;
  OtlpHttpCollector collector_;
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
  std::vector<Trace> expected_query_traces;

  for (const auto& [mode, is_utility, query, expected_spans] : GetTestQueries("test_comment")) {
    auto tp = GenerateTraceparent();
    auto traced_query = Format("/*traceparent='$0'*/ $1;", tp.full, query);
    ASSERT_OK(ExecuteQuery(traced_query, mode));
    expected_query_traces.push_back(
        MakeExpectedTrace(traced_query, tp.trace_id, expected_spans));
  }

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces(expected_query_traces));
}

TEST_F(DistTraceTest, TestTraceparentGuc) {
  for (const auto& [mode, is_utility, query, expected_spans] : GetTestQueries("test_guc")) {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));
    ASSERT_OK(ExecuteQuery(query, mode));

    // Verify immediately, before the next iteration's SET adds its own spans to this trace.
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces(
        {MakeExpectedTrace(query, tp.trace_id, expected_spans)}));
  }
}

TEST_F(DistTraceTest, TestGucPriorityOverComment) {
  const auto& warnings = CaptureWarnings();

  auto guc_tp = GenerateTraceparent();
  auto comment_tp = GenerateTraceparent();

  ASSERT_OK(conn_->ExecuteFormat(
      "SET yb_dist_tracecontext = 'traceparent=''$0'''", guc_tp.full));

  auto query = Format("/*traceparent='$0'*/ SELECT 1;", comment_tp.full);
  ASSERT_OK(conn_->Fetch(query));

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, guc_tp.trace_id,
          {{SpanType::kRoot, 1},
           {SpanType::kParse, 1},
           {SpanType::kRewrite, 1},
           {SpanType::kPlan, 1},
           {SpanType::kExecute, 1},
           {SpanType::kCommit, 1}})}));

  ASSERT_EQ(warnings.size(), 1);
  ASSERT_STR_CONTAINS(warnings.back(),
      "yb_dist_tracecontext GUC takes priority");
}

TEST_F(DistTraceTest, TestTraceparentGucSetLocal) {
  Trace expected_query_trace;

  ASSERT_OK(conn_->Execute("BEGIN"));

  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET LOCAL yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  ASSERT_OK(conn_->Fetch("SELECT 1"));
  expected_query_trace = MakeExpectedTrace("SELECT 1", tp.trace_id,
      {{SpanType::kRoot, 1},
       {SpanType::kParse, 1},
       {SpanType::kRewrite, 1},
       {SpanType::kPlan, 1},
       {SpanType::kExecute, 1}});
  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({expected_query_trace}));

  tp = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET LOCAL yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  ASSERT_OK(conn_->Execute("COMMIT"));
  expected_query_trace = MakeExpectedTrace("COMMIT", tp.trace_id,
      {{SpanType::kRoot, 1},
       {SpanType::kParse, 1},
       {SpanType::kRewrite, 1},
       {SpanType::kExecute, 1},
       {SpanType::kCommit, 1}});

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({expected_query_trace}));

  // After COMMIT, SET LOCAL is reverted so the traceparent should be cleared.
  ASSERT_OK(conn_->Fetch("SELECT 2"));
  ASSERT_OK(collector_.VerifyQueryNotTraced("SELECT 2"));
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
  ASSERT_OK(collector_.VerifyNoTracesEmitted());
}

TEST_F(DistTraceTest, TestMalformedTraceparentComment) {
  // Queries with malformed traceparent comments should succeed but produce no spans.
  // Comments that fail extraction emit a WARNING (not an error) to the client.
  const auto& warnings = CaptureWarnings();
  int num_warnings = 0;

  ASSERT_OK(conn_->Fetch(
      "/*a_regular_comment_without_any_trace_context_data_present_in_here*/ SELECT 1;"));

  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->FetchFormat("/*traceparent=$0'XX*/ SELECT 1;", tp.full));
  ASSERT_EQ(warnings.size(), ++num_warnings);
  ASSERT_STR_CONTAINS(warnings.back(), "traceparent value missing opening quote");

  ASSERT_OK(conn_->FetchFormat("/*traceparent='$0XX*/ SELECT 1;", tp.full));
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

  // Invalid leading traceparent takes priority over a valid trailing one.
  // The leading comment is parsed first, fails validation, emits a warning,
  // and the query runs without tracing; the trailing valid traceparent is ignored.
  {
    auto tp_valid = GenerateTraceparent();
    ASSERT_OK(conn_->FetchFormat(
        "/*traceparent='$0'*/ SELECT 1 /*traceparent='$1'*/",
        kTraceparentValueTooShort, tp_valid.full));
    ASSERT_EQ(warnings.size(), ++num_warnings);
    ASSERT_STR_CONTAINS(warnings.back(), "traceparent field doesn't have the correct size");
  }

  ASSERT_OK(collector_.VerifyNoTracesEmitted());
}

TEST_F(DistTraceTest, TestValidEdgeCasesComment) {
  // Leading whitespace before the comment.
  {
    auto tp = GenerateTraceparent();
    auto query = Format("   /*traceparent='$0'*/ SELECT 1;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp.trace_id,
        {{SpanType::kRoot, 1},
         {SpanType::kParse, 1},
         {SpanType::kRewrite, 1},
         {SpanType::kExecute, 1},
         {SpanType::kPlan, 1},
         {SpanType::kCommit, 1}})}));
  }

  // Extra content in the comment before and after traceparent.
  {
    auto tp = GenerateTraceparent();
    auto query = Format(
        "/*some_extra_stuff traceparent='$0' more_stuff_here*/ SELECT 2;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp.trace_id,
        {{SpanType::kRoot, 1},
         {SpanType::kParse, 1},
         {SpanType::kRewrite, 1},
         {SpanType::kExecute, 1},
         {SpanType::kPlan, 1},
         {SpanType::kCommit, 1}})}));
  }

  // Tab and newline whitespace before the comment.
  {
    auto tp = GenerateTraceparent();
    auto query = Format("\t\n /*traceparent='$0'*/ SELECT 3;", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp.trace_id,
        {{SpanType::kRoot, 1},
         {SpanType::kParse, 1},
         {SpanType::kRewrite, 1},
         {SpanType::kExecute, 1},
         {SpanType::kPlan, 1},
         {SpanType::kCommit, 1}})}));
  }
}

TEST_F(DistTraceTest, TestValidEdgeCasesGuc) {
  // Whitespace before traceparent= (strstr still finds the key).
  {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat(
        "SET yb_dist_tracecontext = '   traceparent=''$0'''", tp.full));

    auto query = "SELECT 1;";
    ASSERT_OK(conn_->Fetch(query));
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces(
        {MakeExpectedTrace(query, tp.trace_id,
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 1},
             {SpanType::kCommit, 1}})}));
  }

  // Extra content before and after traceparent= (strstr still finds the key).
  {
    auto tp = GenerateTraceparent();
    ASSERT_OK(conn_->ExecuteFormat(
        "SET yb_dist_tracecontext = 'extra_before traceparent=''$0'' extra_after'", tp.full));

    auto query = "SELECT 2;";
    ASSERT_OK(conn_->Fetch(query));
    ASSERT_OK(collector_.VerifyAgainstCollectorTraces(
        {MakeExpectedTrace(query, tp.trace_id,
            {{SpanType::kRoot, 1},
             {SpanType::kParse, 1},
             {SpanType::kRewrite, 1},
             {SpanType::kPlan, 1},
             {SpanType::kExecute, 1},
             {SpanType::kCommit, 1}})}));
  }
}

TEST_F(DistTraceTest, TestTraceparentGucSetPersistsAcrossCommit) {
  Trace expected_query_trace;

  auto tp_before_txn = GenerateTraceparent();
  ASSERT_OK(
      conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp_before_txn.full));

  ASSERT_OK(conn_->Execute("BEGIN"));

  auto tp1 = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp1.full));

  ASSERT_OK(conn_->Fetch("SELECT 1"));
  expected_query_trace = MakeExpectedTrace("SELECT 1", tp1.trace_id,
    {{SpanType::kRoot, 1},
     {SpanType::kParse, 1},
     {SpanType::kRewrite, 1},
     {SpanType::kPlan, 1},
     {SpanType::kExecute, 1}});
  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({expected_query_trace}));

  auto tp2 = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp2.full));

  ASSERT_OK(conn_->Execute("COMMIT"));
  expected_query_trace = MakeExpectedTrace("COMMIT", tp2.trace_id,
    {{SpanType::kRoot, 1},
     {SpanType::kParse, 1},
     {SpanType::kRewrite, 1},
     {SpanType::kExecute, 1},
     {SpanType::kCommit, 1}});
  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({expected_query_trace}));

  // After COMMIT, the last plain SET (tp2) persists, so SELECT 2 is traced
  // under tp2. Its spans accumulate in tp2's trace alongside COMMIT's spans.
  // Since both commands share a trace, we merge their individually-built
  // expected spans and re-sort before verification.
  ASSERT_OK(conn_->Fetch("SELECT 2"));
  {
    auto select2_trace = MakeExpectedTrace("SELECT 2", tp2.trace_id,
      {{SpanType::kRoot, 1},
       {SpanType::kParse, 1},
       {SpanType::kRewrite, 1},
       {SpanType::kPlan, 1},
       {SpanType::kExecute, 1},
       {SpanType::kCommit, 1}});
    expected_query_trace.spans.insert(expected_query_trace.spans.end(),
        select2_trace.spans.begin(), select2_trace.spans.end());
    std::sort(expected_query_trace.spans.begin(), expected_query_trace.spans.end());
  }
  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({expected_query_trace}));

  // After RESET, traceparent is cleared and queries should not be traced.
  ASSERT_OK(conn_->Execute("RESET yb_dist_tracecontext"));

  ASSERT_OK(conn_->Fetch("SELECT 3"));
  ASSERT_OK(collector_.VerifyQueryNotTraced("SELECT 3"));
}

TEST_F(DistTraceTest, TestFailedGucSetKeepsPreviousValue) {
  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  // Malformed SETs, all should fail, leaving the valid GUC intact.
  ASSERT_NOK(conn_->Execute("SET yb_dist_tracecontext = 'random_garbage'"));
  ASSERT_NOK(conn_->Execute("SET yb_dist_tracecontext = 'traceparent=''too-short'''"));
  for (const auto& tp_value : kInvalidTraceparentValues) {
    ASSERT_NOK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp_value));
  }

  // The failed SET commands above ran under tp and polluted its trace with extra
  // spans. Set a fresh trace_id to verify SELECT 1 on a clean trace. This also
  // proves the GUC mechanism still works after the failed SETs.
  auto tp2 = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp2.full));

  auto query = "SELECT 1;";
  ASSERT_OK(conn_->Fetch(query));
  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp2.trace_id,
          {{SpanType::kRoot, 1},
           {SpanType::kParse, 1},
           {SpanType::kRewrite, 1},
           {SpanType::kExecute, 1},
           {SpanType::kPlan, 1},
           {SpanType::kCommit, 1}}),
  }));
}

TEST_F(DistTraceTest, TestAbortSpan) {
  auto tp = GenerateTraceparent();
  ASSERT_OK(conn_->ExecuteFormat("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full));

  ASSERT_OK(conn_->Execute("BEGIN"));
  ASSERT_NOK(conn_->Execute("SELECT * FROM nonexistent_table"));
  ASSERT_OK(conn_->Execute("ROLLBACK"));

  // All queries (BEGIN, failed SELECT, ROLLBACK) share the same trace_id.
  // Verify the trace exists and contains an abort span from the error recovery.
  ASSERT_OK(collector_.VerifyTraceContainsOpName(tp.trace_id, "abort"));
}

TEST_F(DistTraceTest, TestTraceparentCommentAppended) {
  std::vector<Trace> expected_traces;
  std::vector<std::string> untraced_queries;

  const std::vector<ExpectedSpan> kSelectSpans = {
    {SpanType::kRoot, 1}, {SpanType::kParse, 1}, {SpanType::kRewrite, 1},
    {SpanType::kExecute, 1}, {SpanType::kPlan, 1}, {SpanType::kCommit, 1},
  };

  // Trailing comment (after the query body).
  {
    auto tp = GenerateTraceparent();
    auto query = Format("SELECT 1 /*traceparent='$0'*/", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    expected_traces.push_back(MakeExpectedTrace(query, tp.trace_id, kSelectSpans));
  }

  // Trailing comment with surrounding whitespace.
  {
    auto tp = GenerateTraceparent();
    auto query = Format("SELECT 2 /*traceparent='$0'*/  ", tp.full);
    ASSERT_OK(conn_->Fetch(query));
    expected_traces.push_back(MakeExpectedTrace(query, tp.trace_id, kSelectSpans));
  }

  // Leading traceparent comment wins over trailing one.
  {
    auto tp1 = GenerateTraceparent();
    auto tp2 = GenerateTraceparent();
    auto query = Format(
        "/*traceparent='$0'*/ SELECT 3 /*traceparent='$1'*/", tp1.full, tp2.full);
    ASSERT_OK(conn_->Fetch(query));
    expected_traces.push_back(MakeExpectedTrace(query, tp1.trace_id, kSelectSpans));
  }

  // Mid-query comment must NOT produce a span (only leading/trailing are supported).
  {
    auto tp = GenerateTraceparent();
    untraced_queries.push_back(Format("SELECT /*traceparent='$0'*/ 4", tp.full));
    ASSERT_OK(conn_->Fetch(untraced_queries.back()));
  }

  // Non-traceparent leading comment followed by traceparent in the middle -- not traced.
  // The first /* */ block has no traceparent field so the leading check falls through,
  // and the second comment is not at the query end so the trailing check also misses it.
  {
    auto tp = GenerateTraceparent();
    untraced_queries.push_back(Format("/* abc */ /*traceparent='$0'*/ SELECT 5", tp.full));
    ASSERT_OK(conn_->Fetch(untraced_queries.back()));
  }

  // Traceparent is not the last comment -- the final /* abc */ has no traceparent field
  // so the trailing check finds no traceparent and returns NO_COMMENT.
  {
    auto tp = GenerateTraceparent();
    untraced_queries.push_back(Format("SELECT 6 /*traceparent='$0'*/ /* abc */", tp.full));
    ASSERT_OK(conn_->Fetch(untraced_queries.back()));
  }

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces(expected_traces));
  for (const auto& q : untraced_queries) {
    ASSERT_OK(collector_.VerifyQueryNotTraced(q));
  }
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

// --- Executor node span tests ---
// Verifies that the per-node tracing span fires during query execution for
// scan, join, and miscellaneous executor node types.

TEST_F(DistTraceTest, TestNodeSpans) {
  ASSERT_OK(CreateTable("t", 20));
  ASSERT_OK(CreateTable("t2", 10));

  // ---- Default GUCs ----
  ASSERT_OK(VerifyExecNodeSpan("SELECT * FROM t", "Seq Scan"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT * FROM t LIMIT 5", "Limit"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT * FROM t ORDER BY val", "Sort"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT count(*) FROM t", "Aggregate"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT 1", "Result"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM (SELECT * FROM t LIMIT 10) sub WHERE sub.id > 5",
      "Subquery Scan"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t UNION ALL SELECT * FROM t2", "Append"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT * FROM generate_series(1,5)", "Function Scan"));
  ASSERT_OK(VerifyExecNodeSpan("VALUES (1,'a'), (2,'b')", "Values Scan"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT generate_series(1,3)", "ProjectSet"));
  ASSERT_OK(VerifyExecNodeSpan(
      "WITH cte AS (SELECT 1 AS n) SELECT * FROM cte a, cte b", "CTE Scan"));
  ASSERT_OK(VerifyExecNodeSpan(
      "WITH RECURSIVE r(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM r WHERE n<3) "
      "SELECT * FROM r",
      "Recursive Union"));
  ASSERT_OK(VerifyExecNodeSpan(
      "WITH RECURSIVE r(n) AS (VALUES(1) UNION ALL SELECT n+1 FROM r WHERE n<3) "
      "SELECT * FROM r",
      "WorkTable Scan"));
  ASSERT_OK(VerifyExecNodeSpan(
      "INSERT INTO t VALUES(999, 'test') RETURNING *", "Insert"));
  ASSERT_OK(VerifyExecNodeSpan(
      "UPDATE t SET val = 'updated' WHERE id = 999 RETURNING *", "Update"));
  ASSERT_OK(VerifyExecNodeSpan(
      "DELETE FROM t WHERE id = 999 RETURNING *", "Delete"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t WHERE id = 1 FOR UPDATE", "LockRows"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT id, row_number() OVER() FROM t", "WindowAgg"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT id FROM t INTERSECT SELECT id FROM t", "SetOp"));

  // ---- enable_seqscan = off ----
  ASSERT_OK(conn_->Execute("CREATE INDEX t_val_idx ON t(val)"));
  ASSERT_OK(conn_->Execute("SET enable_seqscan = off"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT * FROM t WHERE val = 'row_1'", "Index Scan"));
  ASSERT_OK(conn_->Execute("VACUUM ANALYZE t"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT val FROM t WHERE val = 'row_1'", "Index Only Scan"));
  ASSERT_OK(conn_->Execute("RESET enable_seqscan"));

  // ---- enable_hashagg = off ----
  ASSERT_OK(conn_->Execute("SET enable_hashagg = off"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT DISTINCT val FROM t", "Unique"));
  ASSERT_OK(VerifyExecNodeSpan("SELECT val FROM t GROUP BY val", "Group"));
  ASSERT_OK(conn_->Execute("RESET enable_hashagg"));

  // ---- enable_hashjoin = off, enable_mergejoin = off ----
  ASSERT_OK(conn_->Execute("SET enable_hashjoin = off"));
  ASSERT_OK(conn_->Execute("SET enable_mergejoin = off"));

  // Nested loop without index: inner scan gets wrapped in Material for rescanning.
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t a JOIN t b ON a.id = b.id", "Materialize"));

  // ---- + enable_material = off ----
  ASSERT_OK(conn_->Execute("SET enable_material = off"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t JOIN t2 ON t.id = t2.id", "Nested Loop"));
  ASSERT_OK(conn_->Execute("RESET enable_material"));

  // ---- transition to enable_nestloop = off, enable_mergejoin = off ----
  ASSERT_OK(conn_->Execute("RESET enable_hashjoin"));
  ASSERT_OK(conn_->Execute("SET enable_nestloop = off"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t JOIN t2 ON t.id = t2.id", "Hash Join"));

  // ---- transition to enable_nestloop = off, enable_hashjoin = off ----
  ASSERT_OK(conn_->Execute("RESET enable_mergejoin"));
  ASSERT_OK(conn_->Execute("SET enable_hashjoin = off"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t JOIN t2 ON t.id = t2.id", "Merge Join"));

  // ---- transition to enable_hashjoin = off, enable_mergejoin = off, enable_material = off ----
  // With an index on t(id), the planner picks YbBatchedNestLoop.
  // t_id_idx must be created after Merge Join to avoid affecting its plan.
  ASSERT_OK(conn_->Execute("RESET enable_nestloop"));
  ASSERT_OK(conn_->Execute("SET enable_mergejoin = off"));
  ASSERT_OK(conn_->Execute("SET enable_material = off"));
  ASSERT_OK(conn_->Execute("CREATE INDEX t_id_idx ON t(id)"));
  ASSERT_OK(VerifyExecNodeSpan(
      "SELECT * FROM t a JOIN t b ON a.id = b.id", "YB Batched Nested Loop"));
  ASSERT_OK(conn_->Execute("RESET enable_hashjoin"));
  ASSERT_OK(conn_->Execute("RESET enable_mergejoin"));
  ASSERT_OK(conn_->Execute("RESET enable_material"));
}

TEST_F(DistTraceTest, TestExtendedQueryProtocolComment) {
  auto ext_conn = ASSERT_RESULT(Connect(false /* simple_query_protocol */));
  auto tp = GenerateTraceparent();

  auto query = Format("/*traceparent='$0'*/ SELECT 1", tp.full);
  ASSERT_OK(ext_conn.Fetch(query));

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp.trace_id,
          {{SpanType::kRoot, 1},
           {SpanType::kExtParse, 1},
           {SpanType::kParse, 2},
           {SpanType::kRewrite, 1},
           {SpanType::kExtBind, 1},
           {SpanType::kPlan, 1},
           {SpanType::kExtDescribe, 1},
           {SpanType::kExtExecute, 1},
           {SpanType::kExecute, 1},
           {SpanType::kExtSync, 1},
           {SpanType::kCommit, 1}}),
  }));
}

TEST_F(DistTraceTest, TestExtendedQueryProtocolGuc) {
  auto ext_conn = ASSERT_RESULT(Connect(false /* simple_query_protocol */));
  auto tp = GenerateTraceparent();

  ASSERT_OK(ext_conn.Execute(
      Format("SET yb_dist_tracecontext = 'traceparent=''$0'''", tp.full)));

  auto query = "SELECT 1";
  ASSERT_OK(ext_conn.Fetch(query));

  ASSERT_OK(collector_.VerifyAgainstCollectorTraces({
      MakeExpectedTrace(query, tp.trace_id,
          {{SpanType::kRoot, 1},
           {SpanType::kExtParse, 1},
           {SpanType::kParse, 2},
           {SpanType::kRewrite, 1},
           {SpanType::kExtBind, 1},
           {SpanType::kPlan, 1},
           {SpanType::kExtDescribe, 1},
           {SpanType::kExtExecute, 1},
           {SpanType::kExecute, 1},
           {SpanType::kExtSync, 1},
           {SpanType::kCommit, 1}}),
  }));
}

}  // namespace yb::pgwrapper
