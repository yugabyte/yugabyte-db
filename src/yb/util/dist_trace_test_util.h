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

#include <array>
#include <cstdint>

#include "yb/util/dist_trace.h"
#include "yb/util/flags.h"

namespace yb::dist_trace {

// Enables distributed tracing for the lifetime of the object, pointed at an unreachable collector.
class ScopedTestDistTrace {
 public:
  ScopedTestDistTrace() { TEST_SetOtelCollectorEndpoint("http://127.0.0.1:1/v1/traces"); }

 private:
  google::FlagSaver flag_saver_;
};

// Sampled, remote trace context whose trace and span id bytes are all `seed`.
inline trace::SpanContext MakeTestSpanContext(uint8_t seed) {
  std::array<uint8_t, trace::TraceId::kSize> trace_id_bytes;
  trace_id_bytes.fill(seed);
  std::array<uint8_t, trace::SpanId::kSize> span_id_bytes;
  span_id_bytes.fill(seed);
  return trace::SpanContext(
      trace::TraceId(nostd::span<const uint8_t, trace::TraceId::kSize>(
          trace_id_bytes.data(), trace_id_bytes.size())),
      trace::SpanId(nostd::span<const uint8_t, trace::SpanId::kSize>(
          span_id_bytes.data(), span_id_bytes.size())),
      trace::TraceFlags(trace::TraceFlags::kIsSampled),
      /* is_remote */ true);
}

}  // namespace yb::dist_trace
