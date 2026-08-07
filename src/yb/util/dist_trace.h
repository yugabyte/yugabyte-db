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

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/nostd/unique_ptr.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"

#include "yb/util/dist_trace_fwd.h"
#include "yb/util/logging.h"

namespace yb::dist_trace {

namespace context = opentelemetry::context;
namespace nostd = opentelemetry::nostd;
namespace trace = opentelemetry::trace;

// Bundles a span with a context token attached to the constructing thread, so work started after it
// inherits the span as parent. DropScope detaches the token, End ends the span; the two are
// independent, so either may run first and from any thread.
struct SpanWithScope {
  explicit SpanWithScope(nostd::shared_ptr<trace::Span> s)
      : span(std::move(s)),
        token(context::RuntimeContext::Attach(
            context::RuntimeContext::GetCurrent().SetValue(trace::kSpanKey, span))) {}

  ~SpanWithScope() { End(); }

  SpanWithScope(SpanWithScope&&) = delete;
  SpanWithScope& operator=(SpanWithScope&&) = delete;
  SpanWithScope(const SpanWithScope&) = delete;
  SpanWithScope& operator=(const SpanWithScope&) = delete;

  void SetAttribute(nostd::string_view key, const opentelemetry::common::AttributeValue& value) {
    if (span) {
      span->SetAttribute(key, value);
    }
  }

  void SetStatus(trace::StatusCode code, nostd::string_view description = "") {
    if (span) {
      span->SetStatus(code, description);
    }
  }

  trace::SpanContext GetContext() const {
    return span ? span->GetContext() : trace::SpanContext::GetInvalid();
  }

  // Detaches the token from the context stack. Only the thread that attached it can pop it, so call
  // this on the constructing thread; elsewhere it is a no-op. The token itself outlives the call.
  void DropScope() {
    if (token) {
      const bool detached = context::RuntimeContext::Detach(*token);
      CHECK(detached) << "SpanWithScope token is not on this thread's context stack";
    }
  }

  void End() {
    if (span && span->IsRecording()) {
      span->End();
    }
  }

  nostd::shared_ptr<trace::Span> span;
  // Destroying it re-runs Detach, which is a no-op once DropScope has popped it.
  nostd::unique_ptr<context::Token> token;
};

using SpanWithScopePtr = std::unique_ptr<SpanWithScope>;

// OTel service.name for the ysql (postgres backend) process, passed to InitDistTrace at startup.
inline constexpr char kYsqlServiceName[] = "ysql";

void InitDistTrace(
    opentelemetry::nostd::string_view service_name, opentelemetry::nostd::string_view node_uuid);
void ShutdownDistTrace();
nostd::shared_ptr<opentelemetry::trace::Tracer> GetDistTracer();
bool IsDistTraceEnabled();
trace::SpanContext GetTraceparentSpanContext(const char* traceparent);

trace::SpanContext GetActiveSpanContext();

bool IsSpanContextValidAndRemote(const trace::SpanContext& span_context);

// Returns true if distributed tracing is enabled and there is an active span in the OTEL context.
bool HasActiveContext();
nostd::shared_ptr<trace::Span> StartSpan(
    std::string_view op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs,
    trace::StartSpanOptions options);
nostd::shared_ptr<trace::Span> StartSpan(
    std::string_view op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs);
nostd::shared_ptr<trace::Span> StartSpan(std::string_view op_name);

// Client span for an outbound RPC, bundled with an activated scope so it becomes current; drains
// pending thread-local attrs onto it. nullptr when no active context.
SpanWithScopePtr StartClientSpanWithScope(std::string_view op_name);

// Span as a remote child of parent_context (from an inbound request) + activated scope --
// the server end of a propagated trace; needs no local active context.
SpanWithScopePtr StartServerSpanWithScope(
    std::string_view op_name, const trace::SpanContext& parent_context);

// Re-establishes parent_context as this thread's active context WITHOUT a new span, so RPCs built
// here nest under it -- for RPCs issued off the origin's thread.
SpanWithScopePtr ActivateParentScope(const trace::SpanContext& parent_context);

// Thread-local attribute buffer for the next RPC span. Producers (e.g. PgSession) add
// attributes here; the OutboundCall Span consumes them when started.
void AddPendingRpcStringAttr(std::string key, std::string value);

}  // namespace yb::dist_trace
