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

// Client span for an outbound RPC, draining the pending attrs onto it; nullptr when tracing is
// off or no context is active. Not made current -- use ScopedAdoptSpan where that is needed.
nostd::shared_ptr<trace::Span> StartClientSpan(std::string_view op_name);

// Thread-local attribute buffer for the next RPC span. Producers (e.g. PgSession) add
// attributes here; the OutboundCall Span consumes them when started.
void AddPendingRpcStringAttr(std::string key, std::string value);

// Makes a span (or a captured parent context) current on this thread for the enclosing block,
// like ScopedAdoptTrace / ADOPT_WAIT_STATE. Stack-only; the span itself may cross threads.
class ScopedAdoptSpan {
 public:
  explicit ScopedAdoptSpan(const nostd::shared_ptr<trace::Span>& span) {
    if (span) {
      scope_.emplace(span);
    }
  }

  // Adopts a context captured elsewhere without starting a span; no-op when it is invalid.
  explicit ScopedAdoptSpan(const trace::SpanContext& parent_context);

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
