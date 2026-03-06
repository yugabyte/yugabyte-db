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
#include <vector>

#include <gtest/gtest.h>

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"

#include "yb/server/webserver.h"
#include "yb/util/format.h"
#include "yb/util/net/sockaddr.h"
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

class OtlpHttpCollector {
 public:
  struct SpanRecord {
    std::string service_name;
    std::string op_name;
    std::string query_text;
    int64_t db_id = 0;
    int64_t user_id = 0;

    bool operator==(const SpanRecord& other) const {
      return service_name == other.service_name && op_name == other.op_name &&
             query_text == other.query_text && db_id == other.db_id &&
             user_id == other.user_id;
    }

    friend std::ostream& operator<<(std::ostream& os, const SpanRecord& s) {
      return os << "{service=" << s.service_name << " op=" << s.op_name
                << " query=" << s.query_text << " db_id=" << s.db_id
                << " user_id=" << s.user_id << "}";
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
    std::lock_guard lock(mutex_);
    std::vector<SpanRecord> root_spans;
    for (const auto& span : spans_) {
      if (span.op_name != "ysql.query") {
        continue;
      }

      if (std::find(queries.begin(), queries.end(), span.query_text) == queries.end()) {
        continue;
      }

      root_spans.push_back(span);
    }

    return root_spans;
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
              .db_id = FindIntAttribute(span.attributes(), "db.id"),
              .user_id = FindIntAttribute(span.attributes(), "user.id"),
          });
        }
      }
    }
  }

  mutable std::mutex mutex_;
  std::vector<SpanRecord> spans_;
  std::string endpoint_;
  std::unique_ptr<Webserver> server_;
};

class DistTraceTest : public LibPqTestBase {
 public:
  virtual ~DistTraceTest() = default;

  static constexpr auto kOtelBatchMaxQueueSize = 25;
  static constexpr auto kOtelBatchMaxExportBatchSize = 10;
  static constexpr auto kOtelBatchScheduleDelayMs = 100;

  void SetUp() override {
    LibPqTestBase::SetUp();
    conn_ = ASSERT_RESULT(Connect(true /* simple_query_protocol */));
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

  std::optional<pgwrapper::PGConn> conn_;
  OtlpHttpCollector receiver_;
};

}  // namespace

TEST_F(DistTraceTest, TestSpanEmission) {
  const std::vector<std::string> queries = {
      "create table test (id int, val text);",
      "insert into test values (1, 'hello');",
      "select * from test;",
      "update test set val = 'world' where id = 1;",
      "delete from test where id = 1;",
      "alter table test add column extra int;",
  };

  auto db_oid_result = ASSERT_RESULT(conn_->Fetch(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));
  auto db_oid = static_cast<int64_t>(std::stoul(PQgetvalue(db_oid_result.get(), 0, 0)));

  auto user_oid_result = ASSERT_RESULT(conn_->Fetch(
      "SELECT oid FROM pg_authid WHERE rolname = current_user"));
  auto user_oid = static_cast<int64_t>(std::stoul(PQgetvalue(user_oid_result.get(), 0, 0)));

  std::vector<OtlpHttpCollector::SpanRecord> expected_spans;

  for (const auto& query : queries) {
    if (query.rfind("select", 0) == 0) {
      ASSERT_RESULT(conn_->Fetch(query));
    } else {
      ASSERT_OK(conn_->Execute(query));
    }
    expected_spans.push_back(OtlpHttpCollector::SpanRecord{
        .service_name = "ysql", .op_name = "ysql.query",
        .query_text = query, .db_id = db_oid, .user_id = user_oid});
  }

  SleepFor(kOtelBatchScheduleDelayMs * kTimeMultiplier * 2ms);

  ASSERT_EQ(expected_spans, receiver_.GetRootSpansForTestQueries(queries));
}

}  // namespace yb::pgwrapper
