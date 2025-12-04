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

#include "yb/util/otel_tracing.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <sstream>

#include <opentelemetry/exporters/ostream/span_exporter_factory.h>
#include <opentelemetry/sdk/trace/processor.h>
#include <opentelemetry/sdk/trace/simple_processor_factory.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/propagation/http_trace_context.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/trace/span.h>
#include <opentelemetry/trace/span_context.h>
#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/context/propagation/text_map_propagator.h>
#include <opentelemetry/context/propagation/global_propagator.h>
#include <opentelemetry/context/runtime_context.h>

#include "yb/util/logging.h"
#include "yb/util/otel_http_exporter.h"
#include "yb/util/status.h"

// -------------------------------------------------------------------------------------------------
// Bridging stubs for PostgreSQL traceparent helpers
//
// The PostgreSQL backend defines real C functions with these names in
// src/postgres/src/backend/utils/misc/pg_yb_utils.c. We also link a number of
// non-Postgres tools (e.g. log-dump, protoc-gen-yrpc) against libyb_pggate.so,
// which declares these symbols but does not link against the Postgres backend.
//
// Because YugabyteDB executables are linked with -Wl,--no-allow-shlib-undefined,
// any shared library on the link line must have all of its undefined references
// resolved. To avoid forcing every non-Postgres tool to link against the
// Postgres backend library, we provide weak, no-op fallbacks here in libyb_util.
//
// When the Postgres backend is linked into a process, its strong definitions of
// these functions override these weak stubs, so query-level traceparent
// propagation continues to work as intended.
// -------------------------------------------------------------------------------------------------
extern "C" {

__attribute__((weak)) const char* YbGetCurrentTraceparent(void) {
  // No active traceparent in non-Postgres contexts.
  return "";
}

__attribute__((weak)) void YbClearTraceparent(void) {
  // No-op when running outside the Postgres backend.
}

}  // extern "C"

namespace nostd = opentelemetry::nostd;
namespace trace_api = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace ostream_exporter = opentelemetry::exporter::trace;

namespace yb {

namespace otel_internal {

struct SpanContext {
  bool active = false;
  nostd::shared_ptr<trace_api::Span> span;
  nostd::shared_ptr<trace_api::Scope> scope;

  std::string name;
  std::string parent_span_id;
  int64_t start_time_ns = 0;
  int span_kind = 1;
  int status_code = 0;
  std::string status_message;
  std::vector<std::pair<std::string, std::string> > string_attributes;
  std::vector<std::pair<std::string, int64_t> > int_attributes;
};

}  // namespace otel_internal

namespace {

// Global state
std::atomic<bool> g_otel_enabled{false};
std::string g_service_name;
std::mutex g_init_mutex;
nostd::shared_ptr<trace_api::TracerProvider> g_tracer_provider;
nostd::shared_ptr<trace_api::Tracer> g_tracer;

// Helper to convert trace ID to 32-char hex string
std::string TraceIdToHex(const trace_api::TraceId& trace_id) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < trace_api::TraceId::kSize; ++i) {
    oss << std::setw(2) << static_cast<int>(trace_id.Id()[i]);
  }
  return oss.str();
}

// Helper to convert span ID to 16-char hex string
std::string SpanIdToHex(const trace_api::SpanId& span_id) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (size_t i = 0; i < trace_api::SpanId::kSize; ++i) {
    oss << std::setw(2) << static_cast<int>(span_id.Id()[i]);
  }
  return oss.str();
}

// Get current time in nanoseconds since Unix epoch
int64_t GetCurrentTimeNanos() {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

}  // anonymous namespace

OtelSpanHandle::OtelSpanHandle() : context_(nullptr) {}

OtelSpanHandle::OtelSpanHandle(std::unique_ptr<otel_internal::SpanContext> context)
    : context_(std::move(context)) {}

OtelSpanHandle::~OtelSpanHandle() {
  End();  // Safety net - end span if not already ended
}

OtelSpanHandle::OtelSpanHandle(OtelSpanHandle&& other) noexcept
    : context_(std::move(other.context_)) {}

OtelSpanHandle& OtelSpanHandle::operator=(OtelSpanHandle&& other) noexcept {
  if (this != &other) {
    End();  // End current span before replacing
    context_ = std::move(other.context_);
  }
  return *this;
}

void OtelSpanHandle::End() {
  if (!context_ || !context_->active || !context_->span) {
    return;
  }

  // Capture end time and send span data
  if (GetGlobalOtlpSender().IsEnabled()) {
    SimpleSpanData span_data;
    auto span_context = context_->span->GetContext();
    span_data.trace_id = TraceIdToHex(span_context.trace_id());
    span_data.span_id = SpanIdToHex(span_context.span_id());
    span_data.parent_span_id = context_->parent_span_id;
    span_data.name = context_->name;
    span_data.start_time_ns = context_->start_time_ns;
    span_data.end_time_ns = GetCurrentTimeNanos();
    span_data.kind = context_->span_kind;
    span_data.status_code = context_->status_code;
    span_data.status_message = context_->status_message;
    span_data.string_attributes = context_->string_attributes;
    span_data.int_attributes = context_->int_attributes;

    if (!GetGlobalOtlpSender().SendSpan(span_data)) {
      LOG(WARNING) << "[OTEL] Failed to send span via HTTP: " << span_data.name;
    }
  }

  context_->span->End();
  context_->active = false;  // Mark as ended so End() is idempotent
}

void OtelSpanHandle::SetStatus(bool ok, const std::string& description) {
  if (!context_ || !context_->active) return;

  context_->status_code = ok ? 1 : 2;
  context_->status_message = description;

  if (context_->span) {
    if (ok) {
      context_->span->SetStatus(trace_api::StatusCode::kOk, description);
    } else {
      context_->span->SetStatus(trace_api::StatusCode::kError, description);
    }
  }
}

void OtelSpanHandle::SetAttribute(const std::string& key, const std::string& value) {
  if (!context_ || !context_->active) return;

  context_->string_attributes.push_back(std::make_pair(key, value));

  if (context_->span) {
    context_->span->SetAttribute(key, value);
  }
}

void OtelSpanHandle::SetAttribute(const std::string& key, int64_t value) {
  if (!context_ || !context_->active) return;

  context_->int_attributes.push_back(std::make_pair(key, value));

  if (context_->span) {
    context_->span->SetAttribute(key, value);
  }
}

void OtelSpanHandle::SetAttribute(const std::string& key, double value) {
  if (!context_ || !context_->active) return;
  if (context_->span) {
    context_->span->SetAttribute(key, value);
  }
}

void OtelSpanHandle::SetAttribute(const std::string& key, bool value) {
  if (!context_ || !context_->active) return;
  if (context_->span) {
    context_->span->SetAttribute(key, value);
  }
}

bool OtelSpanHandle::IsActive() const {
  return context_ && context_->active;
}

Status OtelTracing::InitFromEnv(const std::string& default_service_name) {
  std::lock_guard<std::mutex> lock(g_init_mutex);

  g_service_name = default_service_name;

  const char* debug_env = std::getenv("OTEL_EXPORTER_DEBUG");
  bool use_debug_exporter = debug_env && (std::string(debug_env) == "true" || 
                                           std::string(debug_env) == "1" ||
                                           std::string(debug_env) == "TRUE");

  if (use_debug_exporter) {
    LOG(INFO) << "[OTEL] Debug mode enabled (OTEL_EXPORTER_DEBUG=true), using OStream exporter";
  } else {
    InitGlobalOtlpSenderFromEnv(g_service_name);
  }

  try {
    auto exporter = ostream_exporter::OStreamSpanExporterFactory::Create();
    if (!exporter) {
      return STATUS(RuntimeError, "Failed to create ostream exporter");
    }

    auto processor = trace_sdk::SimpleSpanProcessorFactory::Create(std::move(exporter));
    if (!processor) {
      return STATUS(RuntimeError, "Failed to create span processor");
    }

    auto provider = trace_sdk::TracerProviderFactory::Create(std::move(processor));
    if (!provider) {
      return STATUS(RuntimeError, "Failed to create tracer provider");
    }

    g_tracer_provider = nostd::shared_ptr<trace_api::TracerProvider>(provider.release());
    trace_api::Provider::SetTracerProvider(g_tracer_provider);

    g_tracer = g_tracer_provider->GetTracer(g_service_name, "1.0.0");
    if (!g_tracer) {
      return STATUS(RuntimeError, "Failed to get tracer");
    }

    auto propagator = nostd::shared_ptr<opentelemetry::context::propagation::TextMapPropagator>(
        new opentelemetry::trace::propagation::HttpTraceContext());
    opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(propagator);

    g_otel_enabled.store(true, std::memory_order_release);

    if (use_debug_exporter) {
      LOG(INFO) << "[OTEL] Using OStream exporter (debug mode) - spans will be written to stdout";
    } else if (GetGlobalOtlpSender().IsEnabled()) {
      LOG(INFO) << "[OTEL] HTTP exporter is enabled, spans will be sent via HTTP";
    } else {
      LOG(INFO) << "[OTEL] HTTP exporter not configured (set OTEL_EXPORTER_OTLP_ENDPOINT)";
    }

    LOG(INFO) << "[OTEL] OpenTelemetry tracing initialized successfully";

  } catch (const std::exception& e) {
    return STATUS_FORMAT(RuntimeError, "Failed to initialize OpenTelemetry: $0", e.what());
  }

  return Status::OK();
}

void OtelTracing::Shutdown() {
  std::lock_guard<std::mutex> lock(g_init_mutex);

  if (g_otel_enabled.load(std::memory_order_acquire)) {
    LOG(INFO) << "Shutting down OpenTelemetry tracing";
    if (g_tracer_provider) {
      auto* raw_provider = g_tracer_provider.get();
      if (auto* sdk_provider = dynamic_cast<trace_sdk::TracerProvider*>(raw_provider)) {
        sdk_provider->ForceFlush();
      }
    }
    g_tracer = nullptr;
    g_tracer_provider = nullptr;
    g_otel_enabled.store(false, std::memory_order_release);
  }
}

bool OtelTracing::IsEnabled() {
  return g_otel_enabled.load(std::memory_order_acquire);
}

OtelSpanHandle OtelTracing::StartSpan(const std::string& name) {
  if (!IsEnabled()) {
    return OtelSpanHandle();
  }

  auto context = std::make_unique<otel_internal::SpanContext>();
  context->active = true;
  context->name = name;
  context->start_time_ns = GetCurrentTimeNanos();
  context->span_kind = 1;  // internal

  if (g_tracer) {
    auto current_span = trace_api::Tracer::GetCurrentSpan();
    if (current_span && current_span->GetContext().IsValid()) {
      context->parent_span_id = SpanIdToHex(current_span->GetContext().span_id());
      trace_api::StartSpanOptions options;
      options.parent = current_span->GetContext();
      context->span = g_tracer->StartSpan(name, options);
    } else {
      context->span = g_tracer->StartSpan(name);
    }
  }

  return OtelSpanHandle(std::move(context));
}

OtelSpanHandle OtelTracing::StartSpanFromTraceparent(
    const std::string& name,
    const std::string& traceparent) {
  if (!IsEnabled() || traceparent.empty()) {
    return OtelSpanHandle();
  }

  if (traceparent.length() < 55 || traceparent[2] != '-' || traceparent[35] != '-' ||
      traceparent[52] != '-') {
    LOG(WARNING) << "[OTEL] Invalid traceparent format: " << traceparent;
    return OtelSpanHandle();
  }

  std::string parent_span_id = traceparent.substr(36, 16);

  auto context = std::make_unique<otel_internal::SpanContext>();
  context->active = true;
  context->name = name;
  context->parent_span_id = parent_span_id;
  context->start_time_ns = GetCurrentTimeNanos();
  context->span_kind = 2;

  if (g_tracer) {
    class TraceparentCarrier : public opentelemetry::context::propagation::TextMapCarrier {
     public:
      explicit TraceparentCarrier(const std::string& traceparent_value)
          : traceparent_(traceparent_value) {}

      nostd::string_view Get(nostd::string_view key) const noexcept override {
        if (key == "traceparent") {
          return nostd::string_view(traceparent_);
        }
        return "";
      }

      void Set(nostd::string_view key, nostd::string_view value) noexcept override {
        if (key == "traceparent") {
          traceparent_ = std::string(value);
        }
      }

     private:
      std::string traceparent_;
    };

    TraceparentCarrier carrier(traceparent);
    auto propagator = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
    auto new_ctx = propagator->Extract(carrier, current_ctx);

    auto remote_span = opentelemetry::trace::GetSpan(new_ctx);
    auto remote_context = remote_span->GetContext();

    if (remote_context.IsValid()) {
      trace_api::StartSpanOptions options;
      options.parent = remote_context;
      options.kind = trace_api::SpanKind::kServer;
      context->span = g_tracer->StartSpan(name, options);
    } else {
      context->span = g_tracer->StartSpan(name);
    }
  }

  return OtelSpanHandle(std::move(context));
}

void OtelTracing::AdoptSpan(const OtelSpanHandle& span) {
  if (!span.IsActive() || !span.context_ || !span.context_->span) {
    return;
  }
  span.context_->scope = nostd::shared_ptr<trace_api::Scope>(
      new trace_api::Scope(span.context_->span));
}

void OtelTracing::EndCurrentSpan() {
}

bool OtelTracing::HasActiveContext() {
  if (!IsEnabled()) {
    return false;
  }
  auto current_span = trace_api::Tracer::GetCurrentSpan();
  return current_span && current_span->GetContext().IsValid();
}

std::string OtelTracing::GetCurrentTraceparent() {
  if (!IsEnabled()) {
    return "";
  }
  auto current_span = trace_api::Tracer::GetCurrentSpan();
  if (!current_span || !current_span->GetContext().IsValid()) {
    return "";
  }

  auto span_context = current_span->GetContext();
  std::string trace_id = TraceIdToHex(span_context.trace_id());
  std::string span_id = SpanIdToHex(span_context.span_id());
  std::string flags = span_context.IsSampled() ? "01" : "00";

  return "00-" + trace_id + "-" + span_id + "-" + flags;
}

}  // namespace yb
