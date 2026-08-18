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

// A span plus a context token attached to the constructing thread, so later work inherits the span.
struct SpanWithScope {
  explicit SpanWithScope(nostd::shared_ptr<trace::Span> s, bool attach = true)
      : span(std::move(s)) {
    if (attach) {
      token = context::RuntimeContext::Attach(
          context::RuntimeContext::GetCurrent().SetValue(trace::kSpanKey, span));
    }
  }

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

  // Detaches the token; only the attaching thread can pop it, elsewhere it is a no-op.
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

namespace internal {
// Set from otel_collector_traces_endpoint by a flag callback during gflag initialization.
extern bool g_dist_trace_enabled;
}  // namespace internal

inline bool IsDistTraceEnabled() { return internal::g_dist_trace_enabled; }

// Sets otel_collector_traces_endpoint and refreshes g_dist_trace_enabled, for in-process tests.
void TEST_SetOtelCollectorEndpoint(const std::string& endpoint);

trace::SpanContext GetTraceparentSpanContext(const char* traceparent);

// The active span's context, or nullopt if there is no active span.
std::optional<trace::SpanContext> GetActiveSpanContext();

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

// Client span for an outbound RPC, draining the pending attrs; attach=false leaves it non-current.
SpanWithScopePtr StartClientSpanWithScope(std::string_view op_name, bool attach = true);

// Server span as a remote child of parent_context; needs no local active context.
SpanWithScopePtr StartServerSpanWithScope(
    std::string_view op_name, const trace::SpanContext& parent_context);

// Makes parent_context current without starting a span, for RPCs issued off the origin's thread.
SpanWithScopePtr ActivateParentScope(const std::optional<trace::SpanContext>& parent_context);

// Buffers an attribute for the next RPC span started on this thread.
void AddPendingRpcStringAttr(std::string key, std::string value);

// Holds the span context captured where it is constructed, so work that runs on another thread can
// re-parent itself under it. Copying carries the captured context; it does not re-capture.
class TraceParent {
 public:
  TraceParent() : parent_(GetActiveSpanContext()) {}

  // Re-captures the currently active context, for holders constructed off the submitting thread.
  void Capture() { parent_ = GetActiveSpanContext(); }

  // Makes the captured context current for as long as the returned scope is held.
  [[nodiscard]] SpanWithScopePtr Activate() const { return ActivateParentScope(parent_); }

 private:
  std::optional<trace::SpanContext> parent_;
};

}  // namespace yb::dist_trace
