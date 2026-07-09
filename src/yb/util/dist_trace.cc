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

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/sdk/common/global_log_handler.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"

#include "yb/util/dist_trace.h"
#include "yb/util/flag_validators.h"
#include "yb/util/signal_util.h"

DEFINE_NON_RUNTIME_PREVIEW_string(otel_collector_traces_endpoint, "",
    "OTLP HTTP endpoint for the OpenTelemetry collector. When set, distributed tracing is "
    "enabled and spans are exported to this endpoint on each query execution.");

DEFINE_NON_RUNTIME_uint32(otel_batch_max_queue_size, 16384,
    "Maximum number of spans that can be buffered in the batch span processor queue. "
    "Spans arriving after this limit are dropped. Must be greater than 0 and at least as "
    "large as otel_batch_max_export_batch_size.");

DEFINE_NON_RUNTIME_uint32(otel_ysql_batch_max_queue_size, 2048,
    "Like otel_batch_max_queue_size, but used only by the ysql (postgres backend) process, which "
    "runs one batch span processor per connection. Must be greater than 0 and at least as large as "
    "otel_batch_max_export_batch_size.");

DEFINE_NON_RUNTIME_uint32(otel_batch_schedule_delay_ms, 500,
    "Time interval in milliseconds between two consecutive batch exports of spans to the "
    "OpenTelemetry collector. Lower values reduce latency but increase export frequency.");

DEFINE_NON_RUNTIME_uint32(otel_batch_max_export_batch_size, 512,
    "Maximum number of spans exported in a single batch. Must be greater than 0 and no "
    "larger than otel_batch_max_queue_size.");

DEFINE_NON_RUNTIME_string(otel_internal_log_level, "info",
    "Minimum OpenTelemetry SDK internal log level forwarded to YugabyteDB logging. Allowed "
    "values are debug, info, warning, error, and none.");

DEFINE_validator(otel_batch_max_queue_size,
    FLAG_GE_FLAG_VALIDATOR(otel_batch_max_export_batch_size));

DEFINE_validator(otel_ysql_batch_max_queue_size,
    FLAG_GE_FLAG_VALIDATOR(otel_batch_max_export_batch_size));

DEFINE_validator(otel_internal_log_level,
    FLAG_IN_SET_VALIDATOR("debug", "info", "warning", "error", "none"));

namespace yb::dist_trace {

namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource_sdk = opentelemetry::sdk::resource;
namespace otlp_exporter = opentelemetry::exporter::otlp;
namespace internal_log = opentelemetry::sdk::common::internal_log;
namespace context = opentelemetry::context;

namespace {

// Service name for the tracing resource and tracer (e.g. "ysql", "Master", "TabletServer").
static std::string g_service_name;

// The ysql process gets its own queue-size flag; tserver/master share otel_batch_max_queue_size.
static uint32_t EffectiveBatchMaxQueueSize() {
  return g_service_name == kYsqlServiceName ? FLAGS_otel_ysql_batch_max_queue_size
                                            : FLAGS_otel_batch_max_queue_size;
}

// A batch of pending RPC span attributes, owned as plain (key, value) strings.
using PendingRpcSpanAttrs = std::vector<std::pair<std::string, std::string>>;

thread_local PendingRpcSpanAttrs pending_rpc_attrs;

// Moves the pending attributes out of the thread-local buffer, leaving it empty. The returned batch
// owns the strings.
static PendingRpcSpanAttrs ConsumePendingRpcAttrs() {
  PendingRpcSpanAttrs consumed = std::move(pending_rpc_attrs);
  // std::move leaves the source unspecified; force it empty.
  pending_rpc_attrs.clear();
  return consumed;
}

// Builds the OTel-API attribute vector (string_view/AttributeValue pairs) viewing into `attrs`.
std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>> SpanAttrsView(
    const PendingRpcSpanAttrs& attrs) {
  std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>> view;
  view.reserve(attrs.size());
  for (const auto& [key, value] : attrs) {
    view.emplace_back(key, value);
  }
  return view;
}

internal_log::LogLevel GetOtelInternalLogLevel() {
  const auto& flag_value = FLAGS_otel_internal_log_level;
  if (flag_value == "debug") {
    return internal_log::LogLevel::Debug;
  }
  if (flag_value == "info") {
    return internal_log::LogLevel::Info;
  }
  if (flag_value == "warning") {
    return internal_log::LogLevel::Warning;
  }
  if (flag_value == "error") {
    return internal_log::LogLevel::Error;
  }
  if (flag_value == "none") {
    return internal_log::LogLevel::None;
  }
  LOG(DFATAL) << "Unknown otel_internal_log_level: " << flag_value;
  return internal_log::LogLevel::Info;
}

class YbOtelLogHandler : public internal_log::LogHandler {
 public:
  void Handle(
      internal_log::LogLevel level, const char* file, int line, const char* msg,
      const opentelemetry::sdk::common::AttributeMap& attributes) noexcept override {
    (void)attributes;

    const char* safe_file = file ? file : "unknown";
    const char* safe_msg = msg ? msg : "";

    switch (level) {
      case internal_log::LogLevel::Error:
        LOG(ERROR) << "[" << safe_file << ":" << line << "]: " << safe_msg;
        break;
      case internal_log::LogLevel::Warning:
        LOG(WARNING) << "[" << safe_file << ":" << line << "]: " << safe_msg;
        break;
      case internal_log::LogLevel::Info:
        LOG(INFO) << "[" << safe_file << ":" << line << "]: " << safe_msg;
        break;
      case internal_log::LogLevel::Debug:
        LOG(INFO) << "[" << safe_file << ":" << line << "] Debug: " << safe_msg;
        break;
      case internal_log::LogLevel::None:
        break;
    }
  }
};

resource_sdk::Resource CreateResource(
    nostd::string_view service_name, nostd::string_view node_uuid) {
  resource_sdk::ResourceAttributes attrs;
  attrs.SetAttribute("service.name", service_name);
  attrs.SetAttribute("process.pid", static_cast<int64_t>(getpid()));
  attrs.SetAttribute("service.instance.id", node_uuid);

  return resource_sdk::Resource::Create(attrs);
}

auto CreateExporter() {
  otlp_exporter::OtlpHttpExporterOptions opts;
  opts.url = FLAGS_otel_collector_traces_endpoint;
  opts.content_type = otlp_exporter::HttpRequestContentType::kBinary;
  LOG(INFO) << "OTEL: Exporting traces to collector at " << opts.url;
  return otlp_exporter::OtlpHttpExporterFactory::Create(opts);
}

trace_sdk::BatchSpanProcessorOptions MakeBatchProcessorOptions() {
  trace_sdk::BatchSpanProcessorOptions batching_opts;
  batching_opts.max_queue_size = static_cast<size_t>(EffectiveBatchMaxQueueSize());
  batching_opts.schedule_delay_millis =
      std::chrono::milliseconds(FLAGS_otel_batch_schedule_delay_ms);
  batching_opts.max_export_batch_size = static_cast<size_t>(FLAGS_otel_batch_max_export_batch_size);
  return batching_opts;
}

auto CreateProcessor(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter) {
  return trace_sdk::BatchSpanProcessorFactory::Create(
      std::move(exporter), MakeBatchProcessorOptions());
}

Status InitDistTraceProvider(const resource_sdk::Resource& resource_attrs) {
  return WithMaskedYsqlSignals([&resource_attrs]() -> Status {
    auto exporter = CreateExporter();
    auto processor = CreateProcessor(std::move(exporter));

    std::shared_ptr<trace::TracerProvider> provider =
        trace_sdk::TracerProviderFactory::Create(std::move(processor), resource_attrs);

    trace_sdk::Provider::SetTracerProvider(provider);

    return Status::OK();
  });
}

// TextMapCarrier adapter that exposes only the
// supplied (through GUC or comment) traceparent header.
class TraceparentCarrier : public context::propagation::TextMapCarrier {
 public:
  explicit TraceparentCarrier(nostd::string_view traceparent = {})
      : traceparent_(traceparent.data(), traceparent.size()) {}

  nostd::string_view Get(nostd::string_view key) const noexcept override {
    if (key == trace::propagation::kTraceParent) {
      return traceparent_;
    }
    return {};
  }

  void Set(nostd::string_view key, nostd::string_view value) noexcept override {
    if (key == trace::propagation::kTraceParent) {
      traceparent_.assign(value.data(), value.size());
    }
  }

  const std::string& traceparent() const { return traceparent_; }

 private:
  std::string traceparent_;
};

}  // namespace

bool IsDistTraceEnabled() {
  return !FLAGS_otel_collector_traces_endpoint.empty();
}

void InitDistTrace(nostd::string_view service_name, nostd::string_view node_uuid) {
  DCHECK(IsDistTraceEnabled());

  internal_log::GlobalLogHandler::SetLogHandler(
      opentelemetry::nostd::shared_ptr<internal_log::LogHandler>(new YbOtelLogHandler()));

  // OTel macros filter first using GlobalLogHandler::GetLogLevel(). Accepted messages
  // are mapped to LOG(...), where YB logging applies its own routing.
  internal_log::GlobalLogHandler::SetLogLevel(GetOtelInternalLogLevel());

  g_service_name = std::string(service_name);
  auto resource_attrs = CreateResource(service_name, node_uuid);
  const auto status = InitDistTraceProvider(resource_attrs);
  if (!status.ok()) {
    LOG(DFATAL) << "Failed to initialize OpenTelemetry tracing: " << status;
    return;
  }

  context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
      nostd::shared_ptr<context::propagation::TextMapPropagator>(
          new trace::propagation::HttpTraceContext()));

  LOG(INFO) << "OTEL: Initialized tracing for service: " << g_service_name
            << "\nBatchSpanProcessor config: max_queue_size=" << EffectiveBatchMaxQueueSize()
            << ", schedule_delay_ms=" << FLAGS_otel_batch_schedule_delay_ms
            << ", max_export_batch_size=" << FLAGS_otel_batch_max_export_batch_size;
}

void ShutdownDistTrace() {
  DCHECK(IsDistTraceEnabled());

  std::shared_ptr<trace::TracerProvider> none;
  trace::Provider::SetTracerProvider(none);

  LOG(INFO) << "OTEL: Tracing cleaned up";
}

nostd::shared_ptr<opentelemetry::trace::Tracer> GetDistTracer() {
  DCHECK(IsDistTraceEnabled());
  return DCHECK_NOTNULL(trace::Provider::GetTracerProvider()->GetTracer(g_service_name));
}

// A SpanContext is not valid when either its trace ID or span ID is all zeros.
// And it is remote if the span context was propagated from an external process/service.
bool IsSpanContextValidAndRemote(const trace::SpanContext& span_context) {
  return span_context.IsValid() && span_context.IsRemote();
}

// Parse a W3C traceparent string and return a SpanContext.
trace::SpanContext GetTraceparentSpanContext(const char* traceparent) {
  TraceparentCarrier carrier(DCHECK_NOTNULL(traceparent));
  context::Context current_context = context::RuntimeContext::GetCurrent();

  static const auto propagator =
    context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();

  // Store the traceparent information into a new span context,
  // create a span with that span context. Returns the context with the span added to it.
  context::Context parent_context = propagator->Extract(carrier, current_context);

  // Return the SpanContext from the parent context.
  return trace::GetSpan(parent_context)->GetContext();
}

std::string GetActiveTraceparent() {
  if (!HasActiveContext()) {
    return {};
  }
  TraceparentCarrier carrier;
  static const auto propagator =
      context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
  propagator->Inject(carrier, context::RuntimeContext::GetCurrent());
  return carrier.traceparent();
}

bool HasActiveContext() {
  if (!IsDistTraceEnabled()) {
    return false;
  }
  auto current_span = trace::Tracer::GetCurrentSpan();
  return current_span && current_span->GetContext().IsValid();
}

trace::SpanContext GetActiveSpanContext() {
  if (!HasActiveContext()) {
    return trace::SpanContext::GetInvalid();
  }
  return trace::Tracer::GetCurrentSpan()->GetContext();
}

nostd::shared_ptr<trace::Span> StartSpan(
    std::string_view op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs,
    trace::StartSpanOptions options) {
  DCHECK(HasActiveContext());

  return GetDistTracer()->StartSpan(
      nostd::string_view(op_name.data(), op_name.size()), attrs, options);
}

nostd::shared_ptr<trace::Span> StartSpan(
    std::string_view op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>&
        attrs) {
  return StartSpan(op_name, attrs, {});
}

nostd::shared_ptr<trace::Span> StartSpan(std::string_view op_name) {
  return StartSpan(op_name, {});
}

SpanWithScopePtr StartSpanWithScope(
    std::string_view op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs,
    trace::SpanKind kind) {
  if (!HasActiveContext()) {
    return nullptr;
  }
  trace::StartSpanOptions options;
  options.kind = kind;
  return std::make_shared<SpanWithScope>(StartSpan(op_name, attrs, options));
}

SpanWithScopePtr StartSpanWithScope(std::string_view op_name, trace::SpanKind kind) {
  return StartSpanWithScope(op_name, {}, kind);
}

SpanWithScopePtr StartClientSpanWithScope(std::string_view op_name) {
  const auto pending = ConsumePendingRpcAttrs();

  if (!IsDistTraceEnabled()) {
    return nullptr;
  }

  auto current_span = trace::Tracer::GetCurrentSpan();
  if (current_span && current_span->GetContext().IsValid()) {
    return StartSpanWithScope(op_name, SpanAttrsView(pending), trace::SpanKind::kClient);
  }

  return nullptr;
}

SpanWithScopePtr StartServerSpanWithScope(
    std::string_view op_name,
    const trace::SpanContext& parent_context,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>&
        attrs) {
  if (!IsDistTraceEnabled()) {
    return nullptr;
  }
  trace::StartSpanOptions options;
  options.kind = trace::SpanKind::kServer;
  options.parent = parent_context;
  return std::make_shared<SpanWithScope>(GetDistTracer()->StartSpan(
      nostd::string_view(op_name.data(), op_name.size()), attrs, options));
}

SpanWithScopePtr StartServerSpanWithScope(
    std::string_view op_name, const trace::SpanContext& parent_context) {
  return StartServerSpanWithScope(op_name, parent_context, {});
}

SpanWithScopePtr ActivateParentScope(const trace::SpanContext& parent_context) {
  if (!IsDistTraceEnabled() || !parent_context.IsValid()) {
    return nullptr;
  }
  // A non-recording span that merely carries parent_context.
  return std::make_shared<SpanWithScope>(
      nostd::shared_ptr<trace::Span>(new trace::DefaultSpan(parent_context)));
}

void AddPendingRpcStringAttr(std::string key, std::string value) {
  pending_rpc_attrs.emplace_back(std::move(key), std::move(value));
}

}  // namespace yb::dist_trace
