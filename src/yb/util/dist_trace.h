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

#include "yb/util/dist_trace_fwd.h"

namespace yb::dist_trace {

void InitDistTrace(int64_t process_pid, opentelemetry::nostd::string_view node_uuid);
void CleanupDistTrace();
opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> GetDistTracer();
bool IsDistTraceEnabled();
opentelemetry::trace::SpanContext GetTraceparentSpanContext(const char* traceparent);
bool IsSpanContextValidAndRemote(const opentelemetry::trace::SpanContext& span_context);

}  // namespace yb::dist_trace
