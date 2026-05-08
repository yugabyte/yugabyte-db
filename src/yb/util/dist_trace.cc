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

#include "yb/util/dist_trace.h"

#include <deque>
#include <memory>

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/propagation/text_map_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_http_exporter_options.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/trace/provider.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/tracer.h"

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/signal_util.h"

DEFINE_NON_RUNTIME_PREVIEW_string(otel_collector_traces_endpoint, "",
    "OTLP HTTP endpoint for the OpenTelemetry collector. When set, distributed tracing is "
    "enabled and spans are exported to this endpoint on each query execution.");

DEFINE_NON_RUNTIME_uint32(otel_batch_max_queue_size, 2048,
    "Maximum number of spans that can be buffered in the batch span processor queue. "
    "Spans arriving after this limit are dropped. Must be greater than 0 and at least as "
    "large as otel_batch_max_export_batch_size.");

DEFINE_NON_RUNTIME_uint32(otel_batch_schedule_delay_ms, 5000,
    "Time interval in milliseconds between two consecutive batch exports of spans to the "
    "OpenTelemetry collector. Lower values reduce latency but increase export frequency.");

DEFINE_NON_RUNTIME_uint32(otel_batch_max_export_batch_size, 512,
    "Maximum number of spans exported in a single batch. Must be greater than 0 and no "
    "larger than otel_batch_max_queue_size.");

DEFINE_validator(otel_batch_max_queue_size,
    FLAG_GE_FLAG_VALIDATOR(otel_batch_max_export_batch_size));

namespace yb::dist_trace {

namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource_sdk = opentelemetry::sdk::resource;
namespace otlp_exporter = opentelemetry::exporter::otlp;
namespace context = opentelemetry::context;

namespace {

const nostd::string_view ysql_resource_name = "ysql";

// Owns string attribute data and maintains a parallel vector of string_view/AttributeValue pairs
// that can be passed directly to the OTel Tracer::StartSpan API. Uses std::deque for pointer
// stability -- unlike std::vector, deque does not relocate existing elements on insertion, so
// string_views into earlier entries remain valid.
class RpcSpanAttrs {
 public:
  void AddStringAttr(std::string key, std::string value) {
    auto& owned_key = owned_keys_.emplace_back(std::move(key));
    auto& owned_val = owned_values_.emplace_back(std::move(value));
    attrs_.emplace_back(owned_key, owned_val);
  }

  const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs()
      const {
    return attrs_;
  }

  void clear() {
    attrs_.clear();
    owned_keys_.clear();
    owned_values_.clear();
  }

 private:
  std::deque<std::string> owned_keys_;
  std::deque<std::string> owned_values_;
  std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>> attrs_;
};

thread_local RpcSpanAttrs pending_rpc_attrs;

resource_sdk::Resource CreateResource(int64_t process_pid, nostd::string_view node_uuid) {
  resource_sdk::ResourceAttributes attrs;
  attrs.SetAttribute("service.name", ysql_resource_name);
  attrs.SetAttribute("process.pid", process_pid);
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
  batching_opts.max_queue_size = static_cast<size_t>(FLAGS_otel_batch_max_queue_size);
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
  explicit TraceparentCarrier(nostd::string_view traceparent)
      : traceparent_(traceparent) {}

  nostd::string_view Get(nostd::string_view key) const noexcept override {
    if (key == trace::propagation::kTraceParent) {
      return traceparent_;
    }
    return {};
  }

  void Set(nostd::string_view, nostd::string_view) noexcept override {}

 private:
  nostd::string_view traceparent_;
};

}  // namespace

bool IsDistTraceEnabled() {
  return !FLAGS_otel_collector_traces_endpoint.empty();
}

void InitDistTrace(int64_t process_pid, nostd::string_view node_uuid) {
  DCHECK(IsDistTraceEnabled());

  auto resource_attrs = CreateResource(process_pid, node_uuid);
  const auto status = InitDistTraceProvider(resource_attrs);
  if (!status.ok()) {
    LOG(DFATAL) << "Failed to initialize OpenTelemetry tracing: " << status;
    return;
  }

  context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
      nostd::shared_ptr<context::propagation::TextMapPropagator>(
          new trace::propagation::HttpTraceContext()));

  // TODO(#30723): Integrate Otel logs with Yugabyte logs.
  LOG(INFO) << "OTEL: Initialized tracing for service: " << ysql_resource_name
            << "\nBatchSpanProcessor config: max_queue_size=" << FLAGS_otel_batch_max_queue_size
            << ", schedule_delay_ms=" << FLAGS_otel_batch_schedule_delay_ms
            << ", max_export_batch_size=" << FLAGS_otel_batch_max_export_batch_size;
}

void CleanupDistTrace() {
  DCHECK(IsDistTraceEnabled());

  std::shared_ptr<trace::TracerProvider> none;
  trace::Provider::SetTracerProvider(none);

  LOG(INFO) << "OTEL: Tracing cleaned up";
}

nostd::shared_ptr<opentelemetry::trace::Tracer> GetDistTracer() {
  DCHECK(IsDistTraceEnabled());
  return DCHECK_NOTNULL(trace::Provider::GetTracerProvider()->GetTracer(ysql_resource_name));
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

bool HasActiveContext() {
  if (!IsDistTraceEnabled()) {
    return false;
  }
  auto current_span = trace::Tracer::GetCurrentSpan();
  return current_span && current_span->GetContext().IsValid();
}

nostd::shared_ptr<trace::Span> StartSpan(
    const std::string& op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>& attrs,
    trace::StartSpanOptions options) {
  DCHECK(HasActiveContext());

  return GetDistTracer()->StartSpan(op_name, attrs, options);
}

nostd::shared_ptr<trace::Span> StartSpan(
    const std::string& op_name,
    const std::vector<std::pair<nostd::string_view, opentelemetry::common::AttributeValue>>&
        attrs) {
  return StartSpan(op_name, attrs, {});
}

nostd::shared_ptr<trace::Span> StartSpan(const std::string& op_name) {
  return StartSpan(op_name, {});
}

void AddPendingRpcStringAttr(std::string key, std::string value) {
  pending_rpc_attrs.AddStringAttr(std::move(key), std::move(value));
}

const std::vector<std::pair<nostd::string_view,
                            opentelemetry::common::AttributeValue>>& GetPendingRpcAttrPairs() {
  return pending_rpc_attrs.attrs();
}

void ClearPendingRpcAttrs() {
  pending_rpc_attrs.clear();
}

}  // namespace yb::dist_trace
