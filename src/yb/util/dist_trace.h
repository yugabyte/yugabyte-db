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
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"

#include "yb/util/dist_trace_fwd.h"

namespace yb::dist_trace {

namespace nostd = opentelemetry::nostd;
namespace trace = opentelemetry::trace;

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

// The active span as a W3C traceparent string, empty if there is no active span.
std::string GetActiveTraceparent();

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

// Client span for an outbound RPC, draining the pending attrs onto it; nullptr when tracing is
// off or no context is active. Not made current -- use ScopedAdoptSpan where that is needed.
nostd::shared_ptr<trace::Span> StartClientSpan(std::string_view op_name);

// Server span as a remote child of parent_context; needs no local active context.
nostd::shared_ptr<trace::Span> StartServerSpan(
    std::string_view op_name, const trace::SpanContext& parent_context);

// Buffers an attribute for the next RPC span started on this thread.
void AddPendingRpcStringAttr(std::string key, std::string value);

// Holds the span context captured where it is constructed, so work that runs on another thread can
// re-parent itself under it. Copying carries the captured context; it does not re-capture.
class TraceParent {
 public:
  TraceParent() : parent_(GetActiveSpanContext()) {}

  const std::optional<trace::SpanContext>& context() const { return parent_; }

 private:
  std::optional<trace::SpanContext> parent_;
};

// Makes a span (or a captured parent context) current on this thread for the enclosing block,
// like ScopedAdoptTrace / ADOPT_WAIT_STATE. Stack-only; the span itself may cross threads.
class ScopedAdoptSpan {
 public:
  explicit ScopedAdoptSpan(const nostd::shared_ptr<trace::Span>& span) {
    if (span) {
      scope_.emplace(span);
    }
  }

  // Adopts a context captured elsewhere without starting a span; no-op when there is none.
  explicit ScopedAdoptSpan(const std::optional<trace::SpanContext>& parent_context);

  explicit ScopedAdoptSpan(const TraceParent& parent) : ScopedAdoptSpan(parent.context()) {}

  ScopedAdoptSpan(const ScopedAdoptSpan&) = delete;
  ScopedAdoptSpan& operator=(const ScopedAdoptSpan&) = delete;
  ScopedAdoptSpan(ScopedAdoptSpan&&) = delete;
  ScopedAdoptSpan& operator=(ScopedAdoptSpan&&) = delete;
  static void* operator new(size_t) = delete;
  static void* operator new[](size_t) = delete;

 private:
  std::optional<trace::Scope> scope_;
};

}  // namespace yb::dist_trace
