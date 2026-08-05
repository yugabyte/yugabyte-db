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

#include <memory>
#include <optional>
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

// PushScope() makes the executor plan node span, the current span during each
// call so child spans (RPC, shared-memory) nest under it. This prevents creating
// a node span on each tuple.
class OtelNodeSpan : public PgMemctx::Registrable {
 public:
  explicit OtelNodeSpan(nostd::shared_ptr<trace::Span> span)
      : span_(std::move(span)) {}

  // Every node span is ended explicitly: at ExecEndNode, at the message
  // boundary for a suspended portal, by the executor error hooks, or by
  // PortalCleanup of a failed portal. Reaching teardown un-ended means one of
  // those paths was missed, not a case to handle here: ~Span() ends the span
  // (leaving its status unset) and ~Scope detaches, so nothing is lost but the
  // status and an accurate end time.
  ~OtelNodeSpan() override {
    DCHECK(ended_) << "node span reached memctx teardown un-ended";
  }

  void PushScope() {
    DCHECK(!scope_) << "node span scope already active";
    scope_.emplace(span_);
  }

  void PopScope() {
    DCHECK(scope_) << "node span scope not active";
    scope_.reset();
  }

  void End() {
    DCHECK(!scope_) << "node span ended while its scope is still current";
    span_->SetStatus(trace::StatusCode::kOk);
    FinishSpan();
  }

  // Error-path cleanup. A node whose scope is still attached was on the
  // active ExecProcNode call chain when the error longjmp fired, so it (and
  // its ancestors, whose scopes are also still attached) genuinely failed.
  // An idle node's calls all returned normally before the failure: leave its
  // status unset, ExecEndNode never confirmed completion, so kOk would
  // overclaim and mark that error cleanup ended it.
  void EndOnError() {
    if (scope_) {
      span_->SetStatus(trace::StatusCode::kError, "node interrupted by error");
      // The longjmp skipped PopScope(); detach before ending so the
      // RuntimeContext never holds an ended span as its current span.
      scope_.reset();
    } else {
      span_->SetAttribute("yb.ended_by_error_cleanup", true);
    }
    FinishSpan();
  }

 private:
  void FinishSpan() {
    ended_ = true;
    span_->End();
  }

  nostd::shared_ptr<trace::Span> span_;
  std::optional<trace::Scope> scope_;
  bool ended_ = false;
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

void YBCInitDistTrace(const char* node_uuid) {
  dist_trace::InitDistTrace(dist_trace::kYsqlServiceName, DCHECK_NOTNULL(node_uuid));
}

void YBCShutdownDistTrace() {
  YBCDistTraceClearStack();
  dist_trace::ShutdownDistTrace();
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

// The attribute setters target the RuntimeContext's current span rather than
// the scope-stack top: node spans are made current via OtelNodeSpan::PushScope
// without going through the stack, so during ExecProcNode this is the only way
// node-level attributes (e.g. sort.type) reach the node span instead of the
// enclosing execute span. Stack spans install a trace::Scope too, so outside
// node execution both notions of "current" coincide.
void YBCDistTraceSetCurrSpanAttrUint64(const char* key, uint64_t value) {
  DCHECK(!YBCIsOtelScopeStackEmpty());
  trace::Tracer::GetCurrentSpan()->SetAttribute(key, value);
}

void YBCDistTraceSetCurrSpanAttrStr(const char* key, const char* value) {
  DCHECK(!YBCIsOtelScopeStackEmpty());
  trace::Tracer::GetCurrentSpan()->SetAttribute(key, value);
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

// Executor plan-node spans (one span per node, not per tuple): created on the
// node's first ExecProcNode call, current for the duration of each call, and
// ended at ExecEndNode, at the end of the protocol message if the portal is
// suspended (so a span never outlives its message's root span), or by the
// executor hooks in yb_dist_trace.c on query abort.
YbcOtelNodeSpan YBCDistTraceCreateNodeSpan(const char* op_name) {
  DCHECK(YBCIsDistTraceActive());
  // StartSpan inherits the implicit context (the parent node's span, or the
  // enclosing execute span for the root node). Ownership lives in the current
  // YB memctx (es_query_cxt); the End functions destroy it early.
  auto node_span = std::make_unique<OtelNodeSpan>(dist_trace::StartSpan(op_name));
  auto* raw = node_span.get();
  YBCGetPgCallbacks()->GetCurrentYbMemctx()->Register(node_span.release());
  return raw;
}

void YBCDistTraceNodeSpanPushScope(YbcOtelNodeSpan node_span) {
  DCHECK_NOTNULL(node_span)->PushScope();
}

void YBCDistTraceNodeSpanPopScope(YbcOtelNodeSpan node_span) {
  DCHECK_NOTNULL(node_span)->PopScope();
}

void YBCDistTraceEndNodeSpan(YbcOtelNodeSpan node_span) {
  DCHECK_NOTNULL(node_span)->End();
  PgMemctx::Destroy(node_span);
}

void YBCDistTraceEndNodeSpanOnError(YbcOtelNodeSpan node_span) {
  DCHECK_NOTNULL(node_span)->EndOnError();
  PgMemctx::Destroy(node_span);
}

} // extern "C"
} // namespace yb::pggate
