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

#include <memory>
#include <string>
#include <unordered_map>

#include "yb/util/status.h"

namespace yb {

// Minimal OpenTelemetry tracing wrapper for YugabyteDB.
// This provides a simple interface for creating distributed traces with minimal overhead.
// When OTEL is disabled (default), all operations become no-ops with minimal cost.

// Forward declarations to avoid exposing OTEL types in headers
namespace otel_internal {
struct SpanContext;
}  // namespace otel_internal

// Opaque handle for an active span. Use RAII semantics - the span ends when destroyed.
class OtelSpanHandle {
 public:
  OtelSpanHandle();
  ~OtelSpanHandle();

  // Move-only type
  OtelSpanHandle(OtelSpanHandle&& other) noexcept;
  OtelSpanHandle& operator=(OtelSpanHandle&& other) noexcept;

  OtelSpanHandle(const OtelSpanHandle&) = delete;
  OtelSpanHandle& operator=(const OtelSpanHandle&) = delete;

  // Set the span status. Call before the span ends (before destruction).
  // ok=true means success, ok=false means error.
  void SetStatus(bool ok, const std::string& description = "");

  // Add an attribute to the span
  void SetAttribute(const std::string& key, const std::string& value);
  void SetAttribute(const std::string& key, int64_t value);
  void SetAttribute(const std::string& key, double value);
  void SetAttribute(const std::string& key, bool value);

  // Check if this handle represents an active span
  bool IsActive() const;

 private:
  friend class OtelTracing;
  explicit OtelSpanHandle(std::unique_ptr<otel_internal::SpanContext> context);
  
  std::unique_ptr<otel_internal::SpanContext> context_;
};

// Main interface for OpenTelemetry tracing
class OtelTracing {
 public:
  // Initialize OpenTelemetry tracing for the current process.
  // Reads configuration from environment variables:
  //   YB_ENABLE_OTEL_TRACING=1         - Enable tracing (default: disabled)
  //   OTEL_EXPORTER_OTLP_ENDPOINT      - OTLP endpoint (default: http://localhost:4317)
  //   OTEL_SERVICE_NAME                - Override service name
  //
  // Should be called once during process startup, before any spans are created.
  // Returns OK on success, or an error if initialization fails.
  static Status InitFromEnv(const std::string& default_service_name);

  // Shutdown OpenTelemetry tracing. Should be called during process shutdown.
  static void Shutdown();

  // Check if OpenTelemetry tracing is enabled
  static bool IsEnabled();

  // Start a new span with the given name.
  // If a span is currently active on this thread, the new span becomes its child.
  // Returns an RAII handle that ends the span when destroyed.
  static OtelSpanHandle StartSpan(const std::string& name);

  // Start a span from a W3C traceparent string (for distributed tracing).
  // The traceparent format is: "version-trace_id-span_id-trace_flags"
  // Example: "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"
  //
  // If traceparent is invalid or empty, returns an inactive span handle.
  static OtelSpanHandle StartSpanFromTraceparent(
      const std::string& name,
      const std::string& traceparent);

  // Adopt a span as the current span for this thread.
  // This is used to propagate context across thread boundaries.
  // The span must remain valid until EndCurrentSpan() is called.
  static void AdoptSpan(const OtelSpanHandle& span);

  // End the current span for this thread (restore previous context).
  static void EndCurrentSpan();

  // Check if there's currently an active span context on this thread.
  // This can be used to conditionally create child spans only when there's a parent.
  static bool HasActiveContext();

 private:
  OtelTracing() = delete;
};

// RAII helper to adopt a span for the duration of a scope
class ScopedOtelSpan {
 public:
  explicit ScopedOtelSpan(OtelSpanHandle&& span)
      : span_(std::move(span)) {
    if (span_.IsActive()) {
      OtelTracing::AdoptSpan(span_);
    }
  }

  ~ScopedOtelSpan() {
    if (span_.IsActive()) {
      OtelTracing::EndCurrentSpan();
    }
  }

  ScopedOtelSpan(const ScopedOtelSpan&) = delete;
  ScopedOtelSpan& operator=(const ScopedOtelSpan&) = delete;

  OtelSpanHandle& span() { return span_; }
  const OtelSpanHandle& span() const { return span_; }

 private:
  OtelSpanHandle span_;
};

}  // namespace yb

