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
#include <utility>
#include <vector>

#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"

#include "yb/util/dist_trace_fwd.h"

namespace yb::dist_trace {

namespace nostd = opentelemetry::nostd;
namespace trace = opentelemetry::trace;

void InitDistTrace(int64_t process_pid, opentelemetry::nostd::string_view node_uuid);
void CleanupDistTrace();
nostd::shared_ptr<opentelemetry::trace::Tracer> GetDistTracer();
bool IsDistTraceEnabled();
trace::SpanContext GetTraceparentSpanContext(const char* traceparent);
bool IsSpanContextValidAndRemote(const trace::SpanContext& span_context);

// Returns true if distributed tracing is enabled and there is an active span in the OTEL context.
bool HasActiveContext();
nostd::shared_ptr<trace::Span> StartSpan(
    const std::string& op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs,
    trace::StartSpanOptions options);
nostd::shared_ptr<trace::Span> StartSpan(
    const std::string& op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs);
nostd::shared_ptr<trace::Span> StartSpan(const std::string& op_name);

// Thread-local attribute buffer for the next RPC span. Producers (e.g. PgSession) add
// attributes here; the OutboundCall constructor consumes them when starting a span.
void AddPendingRpcStringAttr(std::string key, std::string value);
const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>&
    GetPendingRpcAttrPairs();
void ClearPendingRpcAttrs();

}  // namespace yb::dist_trace
