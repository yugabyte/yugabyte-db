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
//

#include "yb/yql/pggate/ybc_dist_trace.h"

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span_metadata.h"

#include "yb/util/dist_trace.h"
#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb::pggate {

namespace common = opentelemetry::common;
namespace trace = opentelemetry::trace;
namespace context = opentelemetry::context;
namespace nostd = opentelemetry::nostd;

constexpr size_t kMaxTruncatedQueryLength = 256;

class OtelScope : public PgMemctx::Registrable {
 public:
  explicit OtelScope(nostd::shared_ptr<trace::Span> sp)
      : span_(std::move(sp)), scope_(span_) {}

  ~OtelScope() {
    if (!ended_normally_) {
      span_->SetStatus(trace::StatusCode::kError, "Span did not end normally");
    }
  }

  void SetAttribute(const char* key, const common::AttributeValue& value) {
    span_->SetAttribute(key, value);
  }

  void EndedNormally() { ended_normally_ = true; }

 private:
  nostd::shared_ptr<trace::Span> span_;
  trace::Scope scope_;
  bool ended_normally_ = false;
};

extern "C" {

bool YBCIsDistTraceEnabled() {
  return dist_trace::IsDistTraceEnabled();
}

void YBCInitDistTrace(int64_t process_pid, const char* node_uuid) {
  DCHECK_GT(process_pid, 0);

  dist_trace::InitDistTrace(process_pid, DCHECK_NOTNULL(node_uuid));
}

void YBCCleanupDistTrace() {
  dist_trace::CleanupDistTrace();
}

YbcOtelScope YBCDistTraceStartRootSpan(
    const char* query, const char* traceparent, YbcPgOid db_oid, YbcPgOid user_id) {
  DCHECK(query);

  trace::StartSpanOptions options;

  options.kind = trace::SpanKind::kServer;

  context::Context parent_ctx = dist_trace::ExtractTraceParent(traceparent);
  options.parent = parent_ctx;

  // Safe to use a string_view into query instead of copying because:
  // StartSpan makes a deep copy of all attributes into a separate buffer before returning,
  // so query only needs to remain valid through this call.
  auto span = dist_trace::GetDistTracer()->StartSpan(
      "ysql.query",
      {{"db.id", db_oid},
       {"user.id", user_id},
       {"query.text", nostd::string_view(query, strnlen(query, kMaxTruncatedQueryLength))}},
      options);

  auto scope = std::make_unique<OtelScope>(std::move(span));
  auto* raw = scope.get();
  YBCGetPgCallbacks()->GetCurrentYbMemctx()->Register(scope.release());
  return raw;
}

void YBCDistTraceSetSpanAttributeUint64(YbcOtelScope scope, const char* key, uint64_t value) {
  DCHECK_NOTNULL(scope)->SetAttribute(key, value);
}

void YBCDistTraceEndSpan(YbcOtelScope scope) {
  DCHECK_NOTNULL(scope)->EndedNormally();
  // Destroying the memory context invokes ~Scope, which calls Span::End().
  // End() enqueues already-populated SpanData for the batcher to export to the collector.
  PgMemctx::Destroy(scope);
}

} // extern "C"

} // namespace yb::pggate
