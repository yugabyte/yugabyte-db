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
#include <thread>
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"

#include "yb/util/dist_trace_fwd.h"
#include "yb/util/logging.h"

namespace yb::dist_trace {

namespace nostd = opentelemetry::nostd;
namespace trace = opentelemetry::trace;

// Bundles a span with an activated (thread-local) scope so work started after it inherits it as
// parent; DropScope on the constructing thread before hopping threads. End() is safe from any
// thread.
struct SpanWithScope {
  explicit SpanWithScope(nostd::shared_ptr<trace::Span> s)
      : span(std::move(s)), scope(span) {}

  ~SpanWithScope() { End(); }

  SpanWithScope(SpanWithScope&&) = default;
  SpanWithScope& operator=(SpanWithScope&&) = default;
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

  // Releases the thread-local scope. Must be called on the thread that constructed this object.
  void DropScope() {
    scope.reset();
    owner_thread = {};
  }

  void End() {
    if (span && span->IsRecording()) {
      // The scope must be dropped on its creating thread; catch an unintended thread hop.
      DCHECK(owner_thread == std::thread::id() || std::this_thread::get_id() == owner_thread)
          << "SpanWithScope scope released off its creating thread";
      scope.reset();
      span->End();
    }
  }

  nostd::shared_ptr<trace::Span> span;
  std::optional<trace::Scope> scope;
  std::thread::id owner_thread = std::this_thread::get_id();
};

using SpanWithScopePtr = std::shared_ptr<SpanWithScope>;

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

// Re-establishes parent_context as this thread's active context WITHOUT a new span, so RPCs built
// here nest under it -- for RPCs issued off the origin's thread.
SpanWithScopePtr ActivateParentScope(const trace::SpanContext& parent_context);

// Thread-local attribute buffer for the next RPC span. Producers (e.g. PgSession) add
// attributes here; the OutboundCall Span consumes them when started.
void AddPendingRpcStringAttr(std::string key, std::string value);

}  // namespace yb::dist_trace
