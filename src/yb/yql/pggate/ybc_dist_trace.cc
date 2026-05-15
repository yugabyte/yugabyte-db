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

#include <stack>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/tracer.h"

#include "yb/util/dist_trace.h"
#include "yb/util/logging.h"

#include "yb/yql/pggate/pg_memctx.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb::pggate {

namespace {

namespace context = opentelemetry::context;
namespace common = opentelemetry::common;
namespace trace = opentelemetry::trace;
namespace nostd = opentelemetry::nostd;

constexpr size_t kMaxTruncatedQueryLength = 256;

using OtelScopeEntry = std::pair<
    opentelemetry::trace::Scope,
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>>;

std::stack<OtelScopeEntry>& OtelScopeStack() {
  static std::stack<OtelScopeEntry> stack;
  return stack;
}

}  // namespace

class OtelSpanContext : public PgMemctx::Registrable {
 public:
  explicit OtelSpanContext(trace::SpanContext span_ctx)
      : span_ctx_(std::move(span_ctx)) {}

  const trace::SpanContext& span_ctx() const {
    return span_ctx_;
  }

 private:
  trace::SpanContext span_ctx_;
};

extern "C" {

bool YBCIsDistTraceEnabled() {
  return dist_trace::IsDistTraceEnabled();
}

bool YBCIsDistTraceActive() {
  return YBCIsDistTraceEnabled() && !YBCIsOtelScopeStackEmpty();
}

bool YBCIsTraceParentValidAndRemote(const char* traceparent) {
  auto span_context = dist_trace::GetTraceparentSpanContext(traceparent);
  return dist_trace::IsSpanContextValidAndRemote(span_context);
}

// Validates that the traceparent is in w3c format and
// returns a valid and remote SpanContext registered in the current memory context.
// returns nullptr if the traceparent is invalid or not remote.
YbcOtelSpanContext YBCGetValidSpanContext(const char* traceparent) {
  auto span_ctx = dist_trace::GetTraceparentSpanContext(traceparent);
  if (!dist_trace::IsSpanContextValidAndRemote(span_ctx)) {
    return nullptr;
  }
  // Type conversion from opentelemetry::trace::SpanContext to OtelSpanContext.
  auto yb_span_ctx = std::make_unique<OtelSpanContext>(std::move(span_ctx));
  // Register the span context in the current memory context.
  auto* raw = yb_span_ctx.get();
  YBCGetPgCallbacks()->GetCurrentYbMemctx()->Register(yb_span_ctx.release());
  return raw;
}

void YBCDestroySpanContext(YbcOtelSpanContext span_ctx) {
  PgMemctx::Destroy(span_ctx);
}

void YBCInitDistTrace(int64_t process_pid, const char* node_uuid) {
  DCHECK_GT(process_pid, 0);

  dist_trace::InitDistTrace(process_pid, DCHECK_NOTNULL(node_uuid));
}

void YBCCleanupDistTrace() {
  dist_trace::CleanupDistTrace();
}

void YBCDistTraceClearStack() {
  while (!YBCIsOtelScopeStackEmpty()) {
    // Spans remaining on the stack were interrupted by an ERROR before they could end normally.
    OtelScopeStack().top().second->SetStatus(
        trace::StatusCode::kError, "Span did not end normally");
    OtelScopeStack().pop();
  }
}

void YBCDistTraceStartRootSpan(
    const char* query, YbcOtelSpanContext yb_span_ctx, YbcPgOid db_oid, YbcPgOid user_id) {
  DCHECK(query);
  DCHECK(YBCIsOtelScopeStackEmpty());

  trace::StartSpanOptions options;
  // kServer kind indicates that the span covers server-side handling of a remote request
  // while the client awaits a response.
  options.kind = trace::SpanKind::kServer;
  options.parent = DCHECK_NOTNULL(yb_span_ctx)->span_ctx();

  // Safe to use a string_view into query instead of copying because:
  // StartSpan makes a deep copy of all attributes into a separate buffer before returning,
  // so query only needs to remain valid through this call.
  auto span = dist_trace::GetDistTracer()->StartSpan(
      "query",
      {{"db.id", db_oid},
       {"user.id", user_id},
       {"query.text", nostd::string_view(query, strnlen(query, kMaxTruncatedQueryLength))}},
      options);

  OtelScopeStack().emplace(trace::Scope(span), std::move(span));
}

void YBCDistTraceStartSpan(const char* op_name) {
  auto span = dist_trace::StartSpan(op_name);

  OtelScopeStack().emplace(trace::Scope(span), std::move(span));
}

void YBCDistTraceSetCurrSpanAttrUint64(const char* key, uint64_t value) {
  DCHECK(!YBCIsOtelScopeStackEmpty());
  DCHECK_NOTNULL(OtelScopeStack().top().second)->SetAttribute(key, value);
}

void YBCDistTraceSetCurrSpanAttrStr(const char* key, const char* value) {
  DCHECK(!YBCIsOtelScopeStackEmpty());
  DCHECK_NOTNULL(OtelScopeStack().top().second)->SetAttribute(key, value);
}

void YBCDistTraceEndSpan() {
  DCHECK(!YBCIsOtelScopeStackEmpty());
  OtelScopeStack().top().second->SetStatus(trace::StatusCode::kOk);
  OtelScopeStack().pop();
}

bool YBCDistTraceIsRootSpan() {
  return OtelScopeStack().size() == 1;
}

bool YBCIsOtelScopeStackEmpty() {
  return OtelScopeStack().empty();
}

} // extern "C"
} // namespace yb::pggate
